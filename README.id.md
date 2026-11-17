# MHGU Recomp untuk iOS dan iPadOS

[English](README.md) | [Bahasa Indonesia](README.id.md)

Port **MHGU Recomp** untuk iOS/iPadOS yang dikembangkan komunitas dan dikelola oleh **Moka Illustra**. Proyek ini menggabungkan runtime `gu-native` dan pekerjaan platform iOS agar dapat dijalankan di iPhone dan iPad.

> Ini adalah proyek komunitas. Proyek ini tidak berafiliasi dengan Nintendo atau Capcom, dan bukan rilis resmi dari pengembang upstream.

## Unduh

Unduh IPA terbaru dari [rilis V1.0](https://github.com/mokaillustra/mhgu-recomp-ios/releases/latest). IPA berisi aplikasinya, **bukan file game**.

## Persyaratan

- iPhone atau iPad dengan iOS/iPadOS 15 atau lebih baru.
- File game **Monster Hunter Generations Ultimate v1.4.0** milikmu sendiri.
- Ruang penyimpanan yang cukup untuk aplikasi dan data game.
- Aplikasi pemasang iOS yang dapat menandatangani dan memasang file IPA, atau [LiveContainer](https://livecontainer.github.io/docs/installation) yang sudah disiapkan sesuai panduan resminya.

## Memasang IPA

### Memasang sebagai aplikasi biasa

1. Unduh IPA dari rilis di atas.
2. Impor IPA ke aplikasi sideload tepercaya seperti [AltStore](https://faq.altstore.io/) atau [SideStore](https://docs.sidestore.io/), lalu pasang mengikuti petunjuk aplikasi tersebut.
3. Jika iOS meminta untuk memercayai akun penanda tangan atau mengaktifkan Developer Mode, ikuti petunjuk dari aplikasi pemasang dan Pengaturan iOS.
4. Buka **MHGU Recomp** satu kali. Aplikasi akan membuat foldernya agar terlihat di aplikasi Files.

Aplikasi sideload mungkin perlu diperbarui masa aktifnya atau ditandatangani ulang melalui aplikasi pemasang. Ikuti dokumentasi terbaru dari aplikasi yang kamu gunakan.

### Menjalankannya dengan LiveContainer

1. Pasang dan siapkan build resmi [LiveContainer](https://livecontainer.github.io/docs/installation) mengikuti panduan resminya. Gunakan build dari proyek LiveContainer resmi.
2. Buka LiveContainer, ketuk **+**, lalu impor IPA MHGU Recomp.
3. Jalankan MHGU Recomp satu kali dari LiveContainer.
4. Untuk menemukan folder datanya, tekan lama kartu aplikasi di LiveContainer, buka **Settings → Container**, lalu ketuk **Open Data Folder**. Buka folder `Documents` milik container itu dan gunakan susunan folder `data/` di bawah.

LiveContainer menyimpan data aplikasi di container-nya sendiri. Jangan menaruh file game di samping IPA atau di dalam bundle aplikasi. Jika folder container tidak terlihat seperti yang diharapkan di Files, gunakan pemilih **Settings → System → Game folder** di dalam game dan pilih folder yang langsung berisi `exefs/` dan `romfs/`.

## Menambahkan file game

Gunakan file dari salinan game v1.4.0 milikmu sendiri yang diperoleh secara sah. Jangan unggah file game, key, atau kode game hasil kompilasi ke repo ini atau membagikannya bersama IPA.

Pada pemasangan biasa, lokasi default-nya adalah **Files → On My iPad/iPhone → MHGU Recomp → `data/`**. Di LiveContainer, letakkan `data/` di dalam folder `Documents` milik aplikasi guest. Susunan yang diharapkan:

```text
Documents/
└── data/
    ├── exefs/
    │   ├── main
    │   ├── main.npdm
    │   └── sdk/                 (jika ada di dump milikmu)
    └── romfs/
        └── ...                  (semua folder dan file RomFS v1.4.0)
```

Pastikan `exefs/` dan `romfs/` berada langsung di dalam `data/`. Aplikasi memeriksa apakah `exefs/main` berasal dari v1.4.0 dan apakah RomFS lengkap (sekitar 35.531 file). Jika aplikasi menampilkan layar file belum lengkap, tambahkan file yang diperlukan lalu ketuk **Check Files Again**.

Kamu juga bisa memilih folder lain yang dapat diakses melalui **Settings → System → Game folder**. Pilih folder yang langsung berisi `exefs/` dan `romfs/`; perubahan berlaku setelah game dimulai ulang.

## Kredit dan lisensi

Fork iOS ini tetap mencantumkan proyek dan kontributor yang menjadi dasar pengembangannya:

- [MHGURecomp](https://github.com/morerokk/MHGURecomp) — recompiler dan runtime asli.
- [`gu-native` milik Khorne](https://github.com/khorne-hub/gu-native) — proyek sumber dan pekerjaan runtime mobile.
- [MHGURecomp-iOS](https://github.com/cerroverb/MHGURecomp-iOS) — referensi build dan platform iOS, termasuk kontribusi iOS terdahulu yang dicantumkan di proyek tersebut.
- Integrasi iOS dan fork ini dikelola oleh **Moka Illustra**.

Repo gabungan ini tidak memiliki satu lisensi yang menggantikan lisensi tiap komponennya. Baca [`LICENSE`](LICENSE), [`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES), [`ios/IOS_BASE_MIT_LICENSE.txt`](ios/IOS_BASE_MIT_LICENSE.txt), [`ios/PORT_NOTICES.txt`](ios/PORT_NOTICES.txt), dan ketentuan lisensi upstream sebelum menggunakan atau mendistribusikan ulang kode. Pertahankan setiap pemberitahuan dan dapatkan izin yang diwajibkan oleh ketentuan tersebut.

## Build

IPA iOS dibuat melalui [`.github/workflows/build-ios.yml`](.github/workflows/build-ios.yml). Build aplikasi yang berhasil tidak menyertakan file game; setiap pemain harus menyediakan file kompatibel miliknya sendiri.
