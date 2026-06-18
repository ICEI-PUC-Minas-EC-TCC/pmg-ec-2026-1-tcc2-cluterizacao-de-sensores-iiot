# analysis/ — Simulação, métricas e figuras (Seção 5.2 do TCC)

Simulador fiel ao firmware (`main/`), motor de métricas A–E e figuras de publicação,
sobre um contrato de dados único (sim ↔ hardware).

## Instalar
```
pip install -r analysis/requirements.txt
```

## Gerar as figuras (perfil abstrato — réplica 1:1 do firmware)
```
python -m analysis.run --out analysis/out
```

## Perfil calibrado (energia absoluta a partir das medições do ammeter)
```
python -m analysis.run --profile calibrated --leader-ma 120 --member-ma 25 --idle-ma 8 --out analysis/out
```

## Testes
```
pytest analysis
```

## Conformância (port Python == leader_policy.cpp real)
Requer g++ (ou clang++).
```
pytest analysis/conformance
```

## Fonte dos dados
- Simulação: `analysis/simulator/sim.py` (passo fixo de 100 ms, espelha o loop do firmware).
- Hardware: `analysis/adapters/hardware.py` lê o `sensores_iiot.db` do servidor MQTT
  (métrica E hoje; papel/política/eventos reais no Spec 2).
