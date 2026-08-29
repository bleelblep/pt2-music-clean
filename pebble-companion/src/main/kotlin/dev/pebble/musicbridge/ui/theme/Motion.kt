package dev.pebble.musicbridge.ui.theme

import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.IntOffset

/**
 * The app's motion vocabulary.
 *
 * Everything that moves in this app moves on a spring, and every spring comes from
 * here. That is the whole point of the file: before it, motion was scattered
 * `tween(350, FastOutSlowInEasing)` calls that each had to be found and matched by
 * hand, so nothing quite agreed with anything else and the player — which builds its
 * own expressive scheme internally — moved on a visibly different curve from the
 * shell around it.
 *
 * The specs are Material 3 Expressive's, read from the theme rather than hard-coded,
 * so [Motion] and the vendored player and stock M3 components are all reading the
 * same numbers. The app theme installs `MotionScheme.expressive()`; its tokens are:
 *
 * | | damping | stiffness | |
 * |---|---|---|---|
 * | fast spatial | 0.6 | 800 | quick, visibly springy |
 * | default spatial | 0.8 | 380 | the workhorse |
 * | slow spatial | 0.8 | 200 | large surfaces |
 * | fast effects | 1.0 | 3800 | |
 * | default effects | 1.0 | 1600 | |
 * | slow effects | 1.0 | 800 | |
 *
 * **Spatial vs. effects is the rule that matters.** Anything occupying space —
 * position, size, corner radius, scale — is *spatial* and is allowed to overshoot;
 * that undershoot-and-settle is what reads as "expressive" rather than merely
 * animated. Anything not occupying space — colour, alpha, elevation — is *effects*
 * and is critically damped, because a colour that overshoots just looks like a bug.
 */
object Motion {

    /** Quick and springy. Press feedback, small toggles, icon pops. */
    val spatialFast: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.fastSpatialSpec()

    /** The default for anything that moves or resizes. */
    val spatialDefault: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.defaultSpatialSpec()

    /** For large surfaces — sheets, full-screen transitions. */
    val spatialSlow: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.slowSpatialSpec()

    /** Critically damped. Colour, alpha, anything that does not occupy space. */
    val effectsFast: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.fastEffectsSpec()

    /** The default for colour and alpha. */
    val effectsDefault: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.defaultEffectsSpec()

    /** Slow fades — content swaps, scrims. */
    val effectsSlow: FiniteAnimationSpec<Float>
        @Composable get() = MaterialTheme.motionScheme.slowEffectsSpec()

    /**
     * The same specs, typed for whatever is being animated.
     *
     * `animateDpAsState` wants `FiniteAnimationSpec<Dp>`, `animateColorAsState` wants
     * `<Color>`, slide transitions want `<IntOffset>`; the properties above are pinned
     * to `Float` because a Kotlin property cannot carry a type parameter. Use these
     * from those call sites.
     */
    @Composable
    fun <T> spatial(): FiniteAnimationSpec<T> = MaterialTheme.motionScheme.defaultSpatialSpec()

    @Composable
    fun <T> spatialQuick(): FiniteAnimationSpec<T> = MaterialTheme.motionScheme.fastSpatialSpec()

    @Composable
    fun <T> effects(): FiniteAnimationSpec<T> = MaterialTheme.motionScheme.defaultEffectsSpec()

    @Composable
    fun <T> effectsQuick(): FiniteAnimationSpec<T> = MaterialTheme.motionScheme.fastEffectsSpec()
}

/**
 * Scale-down-on-press, on a fast spatial spring.
 *
 * Give this the *same* [InteractionSource] the element's `clickable` gets, and put it
 * at the head of the modifier chain — ahead of `clip` and `background`, or it will
 * scale the content and leave the shape it sits in standing still.
 *
 * The release is what does the work: a fast spatial spring overshoots slightly on the
 * way back to 1, so a tap ends with a small bounce instead of just stopping. That is
 * the single cheapest thing in the app that makes a row feel like a physical object.
 */
@Composable
fun Modifier.pressBounce(
    interactionSource: InteractionSource,
    pressedScale: Float = 0.965f,
    origin: TransformOrigin = TransformOrigin.Center,
): Modifier {
    val pressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = if (pressed) pressedScale else 1f,
        animationSpec = Motion.spatialFast,
        label = "pressBounce",
    )
    return this.graphicsLayer {
        scaleX = scale
        scaleY = scale
        transformOrigin = origin
    }
}

/**
 * The specs a screen transition needs, resolved once in composition.
 *
 * `AnimatedContent`'s `transitionSpec` is a plain lambda, not a `@Composable` one, so
 * it cannot read `MaterialTheme.motionScheme` itself. Hoist this above the
 * `AnimatedContent` with [rememberScreenMotion] and hand it to [sharedAxisX] /
 * [fadeThrough] inside the spec.
 */
@Immutable
class ScreenMotion(
    val slide: FiniteAnimationSpec<IntOffset>,
    val scale: FiniteAnimationSpec<Float>,
    val fade: FiniteAnimationSpec<Float>,
    val fadeFast: FiniteAnimationSpec<Float>,
)

@Composable
fun rememberScreenMotion(): ScreenMotion {
    val slide = Motion.spatial<IntOffset>()
    val scale = Motion.spatialDefault
    val fade = Motion.effectsDefault
    val fadeFast = Motion.effectsFast
    return remember(slide, scale, fade, fadeFast) {
        ScreenMotion(slide = slide, scale = scale, fade = fade, fadeFast = fadeFast)
    }
}

/**
 * Specs for `LazyItemScope.animateItem`, so every list in the app reacts to its own
 * contents changing the same way.
 *
 * This is the cheapest motion in the app and the one that pays off most: rows do not
 * teleport when a search returns, a playlist entry is removed, or the library filter
 * changes — the survivors slide to their new places on a spatial spring while the
 * arrivals and departures fade. Fades are effects specs, placement is spatial, per the
 * rule in [Motion].
 *
 * Deliberately not a staggered per-row entrance: in a lazy list those re-fire every
 * time a row is scrolled off and back, so what looks lively on first paint turns into
 * a list that will not stop twitching.
 */
@Immutable
class ListItemMotion(
    val fade: FiniteAnimationSpec<Float>,
    val placement: FiniteAnimationSpec<IntOffset>,
)

@Composable
fun rememberListItemMotion(): ListItemMotion {
    val fade = Motion.effectsDefault
    val placement = Motion.spatial<IntOffset>()
    return remember(fade, placement) { ListItemMotion(fade = fade, placement = placement) }
}

/**
 * Screen-to-screen motion: Material's shared-axis X.
 *
 * Both screens travel the same direction on the same spring, the outgoing one only
 * partway, so the pair reads as one sheet of content sliding rather than two
 * unrelated things crossing. Depth comes from the outgoing screen fading and shrinking
 * a little; it does not travel far enough to be mistaken for the incoming one.
 *
 * [forward] is the direction of navigation, not of travel — forward pushes the new
 * screen in from the right, back brings the old one in from the left.
 */
fun <S> AnimatedContentTransitionScope<S>.sharedAxisX(
    motion: ScreenMotion,
    forward: Boolean,
): ContentTransform {
    val enterFrom: (Int) -> Int = { if (forward) it else -it }
    val exitTo: (Int) -> Int = { if (forward) -it / 5 else it / 5 }

    return (
        slideInHorizontally(animationSpec = motion.slide, initialOffsetX = enterFrom) +
            fadeIn(animationSpec = motion.fade) +
            scaleIn(animationSpec = motion.scale, initialScale = 0.92f)
        ) togetherWith (
        slideOutHorizontally(animationSpec = motion.slide, targetOffsetX = exitTo) +
            fadeOut(animationSpec = motion.fade) +
            scaleOut(animationSpec = motion.scale, targetScale = 0.94f)
        )
}

/**
 * Sibling-to-sibling motion: fade through.
 *
 * Used where the two states are peers with no spatial relationship — switching tabs,
 * swapping a loading state for its results. Nothing slides, because there is no
 * direction to slide in; the outgoing content fades out and shrinks fractionally, the
 * incoming one fades in and grows into place.
 */
fun <S> AnimatedContentTransitionScope<S>.fadeThrough(motion: ScreenMotion): ContentTransform =
    (
        fadeIn(animationSpec = motion.fade) +
            scaleIn(animationSpec = motion.scale, initialScale = 0.94f)
        ) togetherWith (
        fadeOut(animationSpec = motion.fadeFast) +
            scaleOut(animationSpec = motion.scale, targetScale = 0.97f)
        )

/**
 * Bottom-edge chrome entering and leaving — the nav bar, the mini player.
 *
 * Rises on a spatial spring so it settles with the same overshoot everything else
 * has, and leaves on the fast one, because chrome getting out of the way should not
 * be something you wait for.
 */
@Composable
fun bottomChromeEnter(): EnterTransition =
    slideInVertically(animationSpec = Motion.spatial<IntOffset>()) { it } +
        fadeIn(animationSpec = Motion.effectsDefault)

@Composable
fun bottomChromeExit(): ExitTransition =
    slideOutVertically(animationSpec = Motion.spatialQuick<IntOffset>()) { it } +
        fadeOut(animationSpec = Motion.effectsFast)
