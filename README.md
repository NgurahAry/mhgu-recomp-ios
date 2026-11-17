<div align="center">
  <img src="https://www.nintendo.com/eu/media/images/10_share_images/games_15/nintendo_switch_4/H2x1_NSwitch_MonsterHunterGenerationsUltimate_image1600w.jpg" alt="Monster Hunter Generations Ultimate" width="100%">
</div>

# MHGU Recomp for iOS and iPadOS

[English installation](#english-installation) | [Panduan instalasi Bahasa Indonesia](#panduan-instalasi-bahasa-indonesia)

An unofficial community iOS/iPadOS port of **MHGU Recomp**, maintained by **Moka Illustra**. It brings the MHGU Recomp runtime and iOS platform work together for iPhone and iPad.

Port komunitas tidak resmi **MHGU Recomp** untuk iOS/iPadOS, dikelola oleh **Moka Illustra**. Proyek ini menggabungkan runtime MHGU Recomp dan pekerjaan platform iOS untuk iPhone dan iPad.

This project is not affiliated with Nintendo or Capcom and is not an official release from the upstream authors. It does not include game data. Each player must provide files from their own compatible game copy.

## What this project brings

- Touchscreen controls with customizable on-screen layout.
- Online multiplayer co-op, including a chat panel, friend list, and chat copy/paste tools.
- iPhone and iPad-focused app integration.

## Project and source credits

- [MHGURecomp](https://github.com/morerokk/MHGURecomp) — original recompiler and runtime.
- [Khorne's gu-native](https://github.com/khorne-hub/gu-native) — source project and mobile runtime work.
- [MHGURecomp-iOS](https://github.com/cerroverb/MHGURecomp-iOS) — iOS build and platform reference, including earlier iOS contributions credited there.
- **Moka Illustra** — iOS integration and maintenance of this fork.

## Download

Get the **MHGUiOS Recomp v1.0.0 stable IPA** from the [latest release](https://github.com/mokaillustra/mhgu-recomp-ios/releases/latest). The release also provides a clean source archive. The IPA does not include game data.

## Requirements

- An iPhone or iPad running iOS/iPadOS 15 or later.
  iPhone atau iPad dengan iOS/iPadOS 15 atau lebih baru.
- Your own **Monster Hunter Generations Ultimate v1.4.0** game files.
  File game **Monster Hunter Generations Ultimate v1.4.0** milikmu sendiri.
- Enough free storage for the app and game data.
  Ruang penyimpanan yang cukup untuk aplikasi dan data game.
- An iOS installer that can sign IPA files, or LiveContainer.
  Aplikasi pemasang iOS yang dapat menandatangani IPA, atau LiveContainer.

## Credits and licenses

This combined repository does not have one license that replaces the component licenses. Read [LICENSE](LICENSE), [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES), [iOS base license](ios/IOS_BASE_MIT_LICENSE.txt), [iOS port notices](ios/PORT_NOTICES.txt), and the upstream terms before using or redistributing code. Preserve all notices and obtain permissions required by those terms.

---

<details id="english-installation">
<summary><strong>English — installation guide</strong></summary>

### Install as a regular app

1. Download the IPA from the [latest release](https://github.com/mokaillustra/mhgu-recomp-ios/releases/latest).
2. Import it into a trusted installer such as [AltStore](https://faq.altstore.io/) or [SideStore](https://docs.sidestore.io/) and follow its instructions.
3. Open **MHGUiOS Recomp** once so its folder appears in Files.

Sideloaded apps may need to be refreshed or re-signed. If you use a free Apple Developer account and have reached SideStore's active-app limit, use LiveContainer to avoid another app slot. [iLoader](https://iloader.app/) is a convenient way to install SideStore and LiveContainer; see the [iLoader setup guide](https://livecontainer.github.io/docs/installation/lc_sidestore).

### Install with LiveContainer

1. Install and configure [LiveContainer](https://livecontainer.github.io/docs/installation) using its official guide.
2. In LiveContainer, tap **+** and import the MHGU Recomp IPA. Launch it once.
3. Long-press its app card, open **Settings → Container**, then tap **Open Data Folder**. Put game data in the guest app's **Documents** folder.

Place your own v1.4.0 game files in `data/`, with `exefs/` and `romfs/` directly inside. Do not upload or share game files, keys, or generated game code.

</details>

<details id="panduan-instalasi-bahasa-indonesia">
<summary><strong>Bahasa Indonesia — panduan instalasi</strong></summary>

### Memasang sebagai aplikasi biasa

1. Unduh IPA dari [rilis terbaru](https://github.com/mokaillustra/mhgu-recomp-ios/releases/latest).
2. Impor ke aplikasi tepercaya seperti [AltStore](https://faq.altstore.io/) atau [SideStore](https://docs.sidestore.io/), lalu ikuti petunjuknya.
3. Buka **MHGUiOS Recomp** satu kali agar foldernya muncul di Files.

Aplikasi sideload mungkin perlu diperbarui masa aktifnya atau ditandatangani ulang. Jika akun Apple Developer gratis milikmu sudah mencapai batas aplikasi aktif SideStore, gunakan LiveContainer agar tidak memakai slot aplikasi tambahan. [iLoader](https://iloader.app/) memudahkan pemasangan SideStore dan LiveContainer; lihat [panduan penyiapan iLoader](https://livecontainer.github.io/docs/installation/lc_sidestore).

### Memasang dengan LiveContainer

1. Pasang dan siapkan [LiveContainer](https://livecontainer.github.io/docs/installation) mengikuti panduan resminya.
2. Di LiveContainer, ketuk **+**, impor IPA MHGU Recomp, lalu jalankan satu kali.
3. Tekan lama kartu aplikasinya, buka **Settings → Container**, lalu ketuk **Open Data Folder**. Letakkan data game di folder **Documents** milik aplikasi guest.

Letakkan file game v1.4.0 milikmu sendiri di `data/`, dengan `exefs/` dan `romfs/` berada langsung di dalamnya. Jangan mengunggah atau membagikan file game, key, maupun kode game hasil kompilasi.

</details>
