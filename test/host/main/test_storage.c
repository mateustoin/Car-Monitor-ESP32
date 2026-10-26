#include <string.h>
#include "unity.h"
#include "nvs.h"
#include "storage.h"

#define NS "carmon"

static void ensure_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, storage_init());
}

static void test_ratios_roundtrip(void)
{
    ensure_init();
    const float in[5] = {12.5f, 22.1f, 31.9f, 41.0f, 50.25f};
    float out[VP_MAX_GEARS] = {0};
    uint8_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, storage_save_ratios("sandero_k7m", in, 5));
    TEST_ASSERT_EQUAL(ESP_OK, storage_load_ratios("sandero_k7m", out, &n));
    TEST_ASSERT_EQUAL_UINT8(5, n);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(in, out, 5);
    TEST_ASSERT_EQUAL(ESP_OK, storage_erase_ratios("sandero_k7m"));
}

static void test_missing_is_not_found(void)
{
    ensure_init();
    float out[VP_MAX_GEARS];
    uint8_t n = 0;
    storage_erase_ratios("nao_existe");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, storage_load_ratios("nao_existe", out, &n));
}

static void test_erase_removes(void)
{
    ensure_init();
    const float in[3] = {10.f, 20.f, 30.f};
    float out[VP_MAX_GEARS];
    uint8_t n;
    TEST_ASSERT_EQUAL(ESP_OK, storage_save_ratios("p_erase", in, 3));
    TEST_ASSERT_EQUAL(ESP_OK, storage_erase_ratios("p_erase"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, storage_load_ratios("p_erase", out, &n));
}

static void test_corrupt_blob_rejected(void)
{
    ensure_init();
    float out[VP_MAX_GEARS];
    uint8_t n;
    const uint8_t junk[12] = {1, 3, 0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4, 5, 6};
    nvs_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NS, NVS_READWRITE, &h));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(h, "r_junk", junk, sizeof junk));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(h));
    nvs_close(h);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, storage_load_ratios("junk", out, &n));
    storage_erase_ratios("junk");
}

static void test_bitflip_rejected(void)
{
    ensure_init();
    const float in[4] = {11.f, 21.f, 31.f, 41.f};
    float out[VP_MAX_GEARS];
    uint8_t n;
    TEST_ASSERT_EQUAL(ESP_OK, storage_save_ratios("flip", in, 4));
    uint8_t raw[64];
    size_t len = sizeof raw;
    nvs_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NS, NVS_READWRITE, &h));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_get_blob(h, "r_flip", raw, &len));
    raw[5] ^= 0x01;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(h, "r_flip", raw, len));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(h));
    nvs_close(h);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, storage_load_ratios("flip", out, &n));
    storage_erase_ratios("flip");
}

static void test_invalid_args(void)
{
    ensure_init();
    const float in[VP_MAX_GEARS + 1] = {1};
    float out[VP_MAX_GEARS];
    uint8_t n;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_save_ratios("p", in, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_save_ratios("p", in, VP_MAX_GEARS + 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_save_ratios(NULL, in, 3));
    /* id de 14 chars gera chave de 16 (limite NVS: 15) */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_save_ratios("abcdefghijklmn", in, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_load_ratios("abcdefghijklmn", out, &n));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, storage_erase_ratios("abcdefghijklmn"));
    /* id de 13 chars gera chave de 15 e cabe */
    TEST_ASSERT_EQUAL(ESP_OK, storage_save_ratios("abcdefghijklm", in, 3));
    TEST_ASSERT_EQUAL(ESP_OK, storage_erase_ratios("abcdefghijklm"));
}

static void test_mute_default_false_then_true(void)
{
    ensure_init();
    TEST_ASSERT_FALSE(storage_get_mute());
    TEST_ASSERT_EQUAL(ESP_OK, storage_set_mute(true));
    TEST_ASSERT_TRUE(storage_get_mute());
    TEST_ASSERT_EQUAL(ESP_OK, storage_set_mute(false));
    TEST_ASSERT_FALSE(storage_get_mute());
}

static void test_mac_roundtrip(void)
{
    ensure_init();
    uint8_t out[6] = {0};
    const uint8_t mac[6] = {0x00, 0x1D, 0xA5, 0x68, 0x98, 0x8B};
    TEST_ASSERT_EQUAL(ESP_OK, storage_set_elm_mac(mac));
    TEST_ASSERT_EQUAL(ESP_OK, storage_get_elm_mac(out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, out, 6);
}

static void test_mac_erase(void)
{
    ensure_init();
    uint8_t out[6];
    const uint8_t mac[6] = {0x00, 0x1D, 0xA5, 0x68, 0x98, 0x8B};
    TEST_ASSERT_EQUAL(ESP_OK, storage_set_elm_mac(mac));
    TEST_ASSERT_EQUAL(ESP_OK, storage_erase_elm_mac());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, storage_get_elm_mac(out));
    /* apagar de novo (já ausente) não é erro */
    TEST_ASSERT_EQUAL(ESP_OK, storage_erase_elm_mac());
}

static void test_profile_id_roundtrip(void)
{
    ensure_init();
    char out[16];
    TEST_ASSERT_EQUAL(ESP_OK, storage_set_profile_id("sandero_k7m"));
    TEST_ASSERT_EQUAL(ESP_OK, storage_get_profile_id(out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("sandero_k7m", out);
    /* buffer pequeno não pode estourar */
    char small[4];
    TEST_ASSERT_NOT_EQUAL(ESP_OK, storage_get_profile_id(small, sizeof small));
}

void run_storage_tests(void)
{
    RUN_TEST(test_ratios_roundtrip);
    RUN_TEST(test_missing_is_not_found);
    RUN_TEST(test_erase_removes);
    RUN_TEST(test_corrupt_blob_rejected);
    RUN_TEST(test_bitflip_rejected);
    RUN_TEST(test_invalid_args);
    RUN_TEST(test_mute_default_false_then_true);
    RUN_TEST(test_mac_roundtrip);
    RUN_TEST(test_mac_erase);
    RUN_TEST(test_profile_id_roundtrip);
}
