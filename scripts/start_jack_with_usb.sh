#!/bin/bash

echo "Matando JACK actual..."
pkill -9 jackd
sleep 2

echo "Iniciando JACK con interfaz USB..."
/opt/homebrew/bin/jackd -X coremidi -d coreaudio -C "USB-Audio" -P "USB-Audio" &
JACK_PID=$!

echo "Esperando a que JACK inicie..."
sleep 3

echo ""
echo "=== PUERTOS DISPONIBLES ==="
/tmp/list_ports

echo ""
echo "=== JACK está corriendo con PID: $JACK_PID ==="
echo "Tu guitarra debería estar conectada ahora."
echo ""
echo "Ejecutá:"
echo "cd '/Users/claudiocataldo/Desktop/Rena AMP'"
echo "./build/load_and_run external/NeuralAmpModelerCore/example_models/lstm.nam"
