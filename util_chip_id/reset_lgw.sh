#!/bin/sh

RESET_PIN=23

echo "Resetando o Gateway SX1302 via GPIO $RESET_PIN (pinctrl)..."

# Define o pino como Saida em Nivel ALTO (dh) e depois Nivel BAIXO (dl)
pinctrl set $RESET_PIN op dh
sleep 0.1
pinctrl set $RESET_PIN op dl
sleep 0.1

echo "Reset concluído."
