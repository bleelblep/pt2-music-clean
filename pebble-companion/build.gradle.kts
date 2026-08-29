import org.jetbrains.kotlin.gradle.dsl.JvmTarget
// Imported rather than written as java.util.Properties: inside the android { } block
// `java` resolves to Gradle's own Java extension, not the JDK package.
import java.util.Properties

plugins {
    id("com.android.application")
    alias(libs.plugins.compose.compiler)
    // Both are required by the vendored PixelPlayer models: Song is @Parcelize,
    // Lyrics is @Serializable. Applied without a version — the root buildscript
    // already puts the Kotlin plugin on the classpath, so `alias` fails to
    // resolve against an "unknown version".
    id("org.jetbrains.kotlin.plugin.parcelize")
    id("org.jetbrains.kotlin.plugin.serialization")
}

android {
    namespace = "dev.pebble.musicbridge"
    compileSdk = 37

    defaultConfig {
        applicationId = "dev.pebble.musicbridge"
        minSdk = 26
        targetSdk = 36
        versionCode = 7
        versionName = "0.7.0"
    }

    compileOptions {
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    kotlin {
        jvmToolchain(21)
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_21)
        }
    }

    buildFeatures {
        buildConfig = false
        resValues = false
        compose = true
    }

    // Release signing is read from keystore.properties at the repo root, which is
    // gitignored. Credentials must never be inlined here: this file is committed, and a
    // password added to it is one `git add` away from being published. Copy
    // keystore.properties.example to keystore.properties and fill it in; without that
    // file the release build is simply left unsigned, so a clone still builds.
    val keystoreProperties = rootProject.file("keystore.properties").takeIf { it.isFile }?.let { file ->
        Properties().apply { file.inputStream().use { load(it) } }
    }

    signingConfigs {
        if (keystoreProperties != null) {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
        }
    }
}

dependencies {
    implementation(project(":innertube"))
    implementation(files("libs/watchimagebridge.aar"))
    implementation(libs.media3)
    implementation(libs.media3.session)
    // googlevideo 403s Media3's built-in HTTP stack on stream URLs that plain OkHttp
    // downloads without complaint, so ExoPlayer is pointed at OkHttp instead.
    implementation(libs.media3.datasource.okhttp)
    // MediaBrowserCompat, for probing Symfonium's legacy search without Media3's
    // ResultReceiver marshalling (see SymfoniumPlaybackService.debugCompatSearch).
    implementation(libs.androidx.media)
    implementation(libs.pebblekit.client)
    implementation(libs.pipepipe.extractor)
    implementation(libs.okhttp)
    implementation(libs.coroutines.core)
    implementation(libs.coroutines.guava)
    implementation(libs.activity)
    implementation(libs.compose.runtime)
    implementation(libs.compose.foundation)
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.util)
    implementation(libs.compose.ui.tooling)
    implementation(libs.compose.animation)
    implementation(libs.material3)
    implementation(libs.viewmodel)
    implementation(libs.viewmodel.compose)
    implementation(libs.coil)
    implementation(libs.coil.network.okhttp)
    // ── Vendored PixelPlayer player UI (see pixelplay/VENDORING.md) ──────────
    // AbsoluteSmoothCornerShape draws the squircles the whole look rests on, and
    // FullPlayerContent's signature takes ImmutableList.
    implementation(libs.kotlinx.collections.immutable)
    implementation(libs.smooth.corner.rect)
    implementation(libs.timber)
    // Icons.Rounded.{Pause,PlayArrow,SkipNext,SkipPrevious,Cloud} — only PlayArrow
    // is in material-icons-core, so the extended set is required. R8 strips the rest.
    implementation(libs.compose.material.icons.extended)
    // Type.kt resolves Montserrat through the Google Fonts provider.
    implementation(libs.compose.ui.text.google.fonts)
    // Theme.kt uses WindowCompat; ColorRoles.kt uses ColorUtils and Bitmap.scale.
    implementation(libs.androidx.core.ktx)
    // FullPlayerContent collects with collectAsStateWithLifecycle.
    implementation(libs.lifecycle.runtime.compose)
    // ColorRoles.kt builds its schemes with MDC's HCT/Quantizer colour utilities.
    implementation(libs.mdc.material)
    // The vendored image loaders are written against Coil 2 (`coil.*`), while the
    // rest of this app is on Coil 3 (`coil3.*`). The two have distinct package
    // names and coexist; carrying both keeps five vendored files byte-identical
    // to upstream, which is worth more than the size a rewrite would save.
    implementation(libs.coil2.compose)
    coreLibraryDesugaring(libs.desugaring)
    testImplementation(libs.junit)
}
