#include "Application/nvs_service.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace service::application::nvs {

static const char *TAG = "NVS_SERVICE";

// Namespace único para os dados de energia persistidos (bateria + orçamento).
static constexpr char NAMESPACE[] = "energy";

void init() {
    esp_err_t err = nvs_flash_init();

    ESP_ERROR_CHECK(err);
}

bool get_float(const char *key, float *out) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    // NVS não tem tipo float: gravado/lido como blob de 4 bytes.
    size_t size = sizeof(float);
    esp_err_t err = nvs_get_blob(handle, key, out, &size);
    nvs_close(handle);
    return err == ESP_OK && size == sizeof(float);
}

void set_float(const char *key, float value) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_set_blob(handle, key, &value, sizeof(value));
    if (err == ESP_OK) {
        nvs_commit(handle);
    } else {
        ESP_LOGW(TAG, "set_float(%s) falhou: %s", key, esp_err_to_name(err));
    }
    nvs_close(handle);
}

bool get_u32(const char *key, uint32_t *out) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_u32(handle, key, out);
    nvs_close(handle);
    return err == ESP_OK;
}

void set_u32(const char *key, uint32_t value) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        nvs_commit(handle);
    } else {
        ESP_LOGW(TAG, "set_u32(%s) falhou: %s", key, esp_err_to_name(err));
    }
    nvs_close(handle);
}

void erase_energy() {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGW(TAG, "Energia persistida apagada (namespace '%s')", NAMESPACE);
}

} // namespace service::application::nvs
