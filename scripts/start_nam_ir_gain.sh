#!/usr/bin/env bash
set -euo pipefail

# RenaAmp one-command runner (auto-JACK, auto-build, NAM+IR with master gain)
# Goal: you just run this script and play.
# Defaults: SR=48000, Buffer=64, MasterGain=+6 dB
# You can still override via flags if you want.
#
# Examples:
#   ./scripts/start_nam_ir_gain.sh                  # auto JACK/build, +6 dB
#   ./scripts/start_nam_ir_gain.sh -g +9            # same, but +9 dB
#   ./scripts/start_nam_ir_gain.sh -r 48000 -p 128  # start JACK at 48k/128
#   ./scripts/start_nam_ir_gain.sh -m <model> -i <ir>

# Defaults
MODEL="resources/models/JCM800_SM57.nam"
IR="resources/irs/G12-65_sm57_LL.wav"
GAIN_DB="+6"              # default master gain for comfortable loudness
SR=48000
BUF=64
NBUFFERS=3
START_JACK_AUTO=1         # auto-start JACK if not running

print_help() {
  cat <<EOF
RenaAmp NAM+IR Runner (one-command)

Options (all optional):
  -m, --model <path>       Ruta al modelo NAM (.nam). Default: $MODEL
  -i, --ir <path>          Ruta al IR (.wav). Default: $IR
  -g, --gain <dB>          Master gain en dB (post NAM/IR, pre-Limiter). Default: $GAIN_DB
  -r, --samplerate <Hz>    JACK sample rate si se inicia. Default: $SR
  -p, --buffer <frames>    JACK buffer si se inicia. Default: $BUF
  -n, --nbuffers <N>       JACK periods si se inicia. Default: $NBUFFERS
  --no-start-jack          No iniciar JACK automáticamente.
  -h, --help               Mostrar ayuda y salir.
EOF
}

# Parse flags (optional)
while [[ $# -gt 0 ]]; do
  case "$1" in
    -m|--model) MODEL="$2"; shift 2;;
    -i|--ir) IR="$2"; shift 2;;
    -g|--gain) GAIN_DB="$2"; shift 2;;
    -r|--samplerate) SR="$2"; shift 2;;
    -p|--buffer) BUF="$2"; shift 2;;
    -n|--nbuffers) NBUFFERS="$2"; shift 2;;
    --no-start-jack) START_JACK_AUTO=0; shift;;
    -h|--help) print_help; exit 0;;
    *) echo "Opción desconocida: $1"; print_help; exit 1;;
  esac
done

# Helper: check if JACK server is running
is_jack_running() {
  if command -v jack_lsp >/dev/null 2>&1; then
    # jack_lsp returns non-zero if server not running
    jack_lsp >/dev/null 2>&1
    return $?
  else
    # Fallback: check process
    pgrep -x jackd >/dev/null 2>&1 || pgrep -x jackdmp >/dev/null 2>&1
    return $?
  fi
}

# 1) Ensure JACK is running (unless disabled)
if [[ "$START_JACK_AUTO" -eq 1 ]]; then
  if ! is_jack_running; then
    echo "[RenaAmp] Iniciando JACK: SR=${SR} Hz, Buffer=${BUF}, N=${NBUFFERS}..."
    killall -9 jackd jackdmp 2>/dev/null || true
    jackd -R -d coreaudio -r "$SR" -p "$BUF" -n "$NBUFFERS" > /tmp/jack.log 2>&1 &
    sleep 2
    if ! is_jack_running; then
      echo "[RenaAmp][ERROR] No se pudo iniciar JACK. Revisa /tmp/jack.log" >&2
      tail -n 60 /tmp/jack.log || true
      exit 1
    fi
  fi
fi

# 2) Ensure Release binary exists; if not, auto-build
BIN="./build/load_and_run"
if [[ ! -x "$BIN" ]]; then
  echo "[RenaAmp] Compilando binario Release (auto-build)..."
  rm -rf build
  cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DRENAAMP_BUILD_TESTS=OFF -DRENAAMP_BUILD_BENCHMARKS=OFF
  cmake --build build --target load_and_run -- -j1
  if [[ ! -x "$BIN" ]]; then
    echo "[RenaAmp][ERROR] No se generó $BIN. Revisa errores de compilación." >&2
    exit 1
  fi
fi

# 3) Run with configured model/IR and master gain
set -x
"$BIN" "$MODEL" "$IR" --master-gain "$GAIN_DB"

echo "[RenaAmp] Cerrando JACK..."
killall jackd jackdmp 2>/dev/null || true