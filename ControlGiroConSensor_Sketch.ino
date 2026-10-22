// ============================================================
// FASE 5: Sensor + Motor + Modos (automático / manual), sin WiFi
// Comandos por Serie (115200):
//   a = automático | m = manual | d = derecha | i = izquierda | s = detener
//   (d, i, s solo funcionan en modo manual)
// ============================================================

// --- PINES (los mismos de las Fases 1 y 2) ---
const int PIN_TRIG = 9;
const int PIN_ECHO = 10;
const int PIN_ENA = 5, PIN_IN1 = 2, PIN_IN2 = 4;   // Motor A
const int PIN_ENB = 6, PIN_IN3 = 7, PIN_IN4 = 8;   // Motor B

// --- PARÁMETROS ---
const int VELOCIDAD = 200;                         // 0-255 (ajústala como en la Fase 2)
const unsigned long INTERVALO_SENSOR_MS = 60;      // Mínimo seguro entre mediciones del HC-SR04
const unsigned long INTERVALO_INFO_MS   = 500;     // Cada cuánto imprimimos el estado
const int N_MUESTRAS = 5;                          // Lecturas para la mediana

// Rangos del modo automático (cm), definidos por el enunciado.
// Van como constantes para cambiarlos en un solo lugar si el profesor los ajusta.
const float DER_MIN = 10.0;   // Desde aquí hasta DER_MAX: gira a la derecha
const float DER_MAX = 25.0;   // Después de esto y hasta IZQ_MAX: gira a la izquierda
const float IZQ_MAX = 40.0;   // Más allá (o menos de DER_MIN): se detiene

// --- ESTADOS ---
// Dos enums: uno para lo que hace el motor y otro para quién lo controla.
// Separarlos evita mezclar "qué hace" con "quién manda", y son justo los dos
// datos que la web mostrará más adelante.
enum EstadoMotor { DETENIDO, DERECHA, IZQUIERDA };
enum Modo { MANUAL, AUTO };

EstadoMotor estadoActual = DETENIDO;
Modo modoActual = MANUAL;     // Arranca en manual y detenido: es lo más seguro al encender

float muestras[N_MUESTRAS];
int indiceMuestra = 0;
float distanciaActual = -1.0; // Última distancia filtrada; la API la enviará a la web
unsigned long ultimaMedicion = 0;
unsigned long ultimaInfo = 0;

// ---------- SENSOR (explicado en la Fase 1) ----------
float medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duracion = pulseIn(PIN_ECHO, HIGH, 30000UL);   // Timeout 30 ms
  if (duracion == 0) return -1.0;                              // -1 = sin eco
  return duracion * 0.0343 / 2.0;
}

float mediana(float *datos, int n) {   // Ignora lecturas absurdas aisladas
  float copia[N_MUESTRAS];
  for (int i = 0; i < n; i++) copia[i] = datos[i];
  for (int i = 1; i < n; i++) {
    float clave = copia[i];
    int j = i - 1;
    while (j >= 0 && copia[j] > clave) { copia[j + 1] = copia[j]; j--; }
    copia[j + 1] = clave;
  }
  return copia[n / 2];
}

// ---------- MOTOR (explicado en la Fase 2) ----------
void aplicarGiro(bool horario) {
  digitalWrite(PIN_IN1, horario ? HIGH : LOW);
  digitalWrite(PIN_IN2, horario ? LOW : HIGH);
  digitalWrite(PIN_IN3, horario ? HIGH : LOW);
  digitalWrite(PIN_IN4, horario ? LOW : HIGH);
  analogWrite(PIN_ENA, VELOCIDAD);
  analogWrite(PIN_ENB, VELOCIDAD);
}

void detener() {
  analogWrite(PIN_ENA, 0);
  analogWrite(PIN_ENB, 0);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
}

// Único punto de control del motor (modo automático, manual y failsafe pasan por aquí).
void mover(EstadoMotor nuevo) {
  if (nuevo == estadoActual) return;
  if (estadoActual != DETENIDO && nuevo != DETENIDO) {   // Al invertir, parada previa
    detener();
    delay(50);
  }
  switch (nuevo) {
    case DERECHA:   aplicarGiro(true);  break;
    case IZQUIERDA: aplicarGiro(false); break;
    case DETENIDO:  detener();          break;
  }
  estadoActual = nuevo;
}

// ---------- LÓGICA NUEVA ----------

// ------------------------------------------------------------
// decidirAutomatico()
// Qué hace: convierte una distancia en lo que debe hacer el motor, según el enunciado.
// Por qué es una función aparte que solo DEVUELVE un estado (no toca el motor):
// así es fácil de probar y de explicar, y mover() sigue siendo el único que
// actúa sobre el hardware.
// Un -1 (sin eco) cae en "fuera de rango", por lo que el motor se detiene:
// si el sensor falla, el motor no queda girando a ciegas.
// ------------------------------------------------------------
EstadoMotor decidirAutomatico(float d) {
  if (d >= DER_MIN && d <= DER_MAX) return DERECHA;
  if (d >  DER_MAX && d <= IZQ_MAX) return IZQUIERDA;
  return DETENIDO;
}

// ------------------------------------------------------------
// cambiarModo()
// Qué hace: cambia entre automático y manual.
// Por qué siempre detiene el motor al cambiar: así el motor nunca "hereda" un giro
// de otro modo. Al pasar a manual, queda parado hasta que el usuario ordene algo;
// al pasar a automático, el sensor decide en su siguiente lectura (≤ 60 ms).
// ------------------------------------------------------------
void cambiarModo(Modo nuevo) {
  if (nuevo == modoActual) return;
  modoActual = nuevo;
  mover(DETENIDO);
  Serial.print(">> Modo: ");
  Serial.println(nuevo == AUTO ? "AUTOMATICO" : "MANUAL");
}

// ------------------------------------------------------------
// procesarComando()
// Qué hace: interpreta una letra de Serie. Es el equivalente local de lo que
// luego harán los comandos de la API (mismas reglas: solo se acepta una lista
// cerrada y los de motor solo valen en manual; la API responderá 409).
// ------------------------------------------------------------
void procesarComando(char c) {
  switch (c) {
    case 'a': cambiarModo(AUTO);   break;
    case 'm': cambiarModo(MANUAL); break;
    case 'd':
    case 'i':
    case 's':
      if (modoActual != MANUAL) {
        Serial.println(">> Ignorado: los comandos de motor solo valen en modo manual");
        break;
      }
      mover(c == 'd' ? DERECHA : c == 'i' ? IZQUIERDA : DETENIDO);
      break;
    default: break;   // Cualquier otro carácter (incluido salto de línea) se ignora
  }
}

void imprimirEstado() {
  Serial.print("Dist: ");
  Serial.print(distanciaActual, 1);
  Serial.print(" cm | Modo: ");
  Serial.print(modoActual == AUTO ? "AUTO" : "MANUAL");
  Serial.print(" | Motor: ");
  Serial.println(estadoActual == DERECHA ? "DERECHA" : estadoActual == IZQUIERDA ? "IZQUIERDA" : "DETENIDO");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_ENA, OUTPUT); pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_ENB, OUTPUT); pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);

  detener();
  for (int i = 0; i < N_MUESTRAS; i++) muestras[i] = -1.0;
  Serial.println("Listo. a=auto | m=manual | d/i/s=motor (solo manual)");
}

void loop() {
  if (Serial.available() > 0) procesarComando(Serial.read());

  // El sensor se lee SIEMPRE, también en manual (lo exige el enunciado:
  // en manual la web sigue mostrando la distancia).
  if (millis() - ultimaMedicion >= INTERVALO_SENSOR_MS) {
    ultimaMedicion = millis();
    muestras[indiceMuestra] = medirDistanciaCm();
    indiceMuestra = (indiceMuestra + 1) % N_MUESTRAS;
    distanciaActual = mediana(muestras, N_MUESTRAS);

    // Solo en automático el sensor manda sobre el motor.
    if (modoActual == AUTO) mover(decidirAutomatico(distanciaActual));
  }

  if (millis() - ultimaInfo >= INTERVALO_INFO_MS) {
    ultimaInfo = millis();
    imprimirEstado();
  }
}