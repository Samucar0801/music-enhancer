# Licenças de terceiros

O núcleo DSP em `native/` **não usa nenhuma biblioteca de terceiros**. Os
filtros, a FFT, os medidores de loudness e o limiter foram escritos do zero,
a partir das especificações públicas citadas abaixo. Nenhum código foi
copiado.

## Dependências da camada Android

| Componente | Licença |
|---|---|
| AndroidX Media3 (ExoPlayer) | Apache-2.0 |
| AndroidX Core, Activity, Lifecycle, DataStore, DocumentFile | Apache-2.0 |
| Jetpack Compose + Material 3 | Apache-2.0 |
| Kotlin stdlib e coroutines | Apache-2.0 |
| kotlinx.serialization | Apache-2.0 |
| Guava (`ListenableFuture`, via Media3) | Apache-2.0 |

Todas são Apache-2.0, compatíveis com a licença deste projeto.

## Especificações usadas (não são código)

- ITU-R BS.1770-4 — medição de loudness e true peak
- EBU Tech 3341 / EBU R128 — casos de calibração e faixa de loudness
- Robert Bristow-Johnson, *Audio EQ Cookbook* — fórmulas de biquad, domínio
  público

## Planejado, ainda não incluído

Nenhum modelo de IA é distribuído com este app. Se a separação de fontes for
implementada, o modelo pretendido é o **HT-Demucs** (Meta, licença MIT),
baixado pelo usuário sob demanda — não embutido no APK.
