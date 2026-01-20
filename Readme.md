# Модель для экспорта метрик

# Для создания метрик

Т.к. плагины при сатической сборке инкапсулирую символы, статические переменные могут быть разные между плагинами,
поэтому следует инициализировать MetricsModel::instance() в каждом плагине общим указателем. 

```cpp
    void registerModels(d3156::PluginCore::ModelsStorage& models) override {
        MetricsModel::instance() = RegisterModel("MetricsModel",new MetricsModel(), MetricsModel);
        
    }
```

После регистрации модели можно создавать метрики в своих классах. Они будут автоматически сами регистрироваться в MetricsModel модели.


