#include "TestFramework.h"
#include "jazz/core/Reharmonizer.h"

#include <cstdlib>
#include <optional>

using namespace jazz::core;

namespace
{
    Chart chartFrom (const std::string& text)
    {
        const auto result = parseProgressionText (text);
        CHECK (result.ok());
        return *result.chart;
    }

    const Substitution* find (const std::vector<Substitution>& substitutions, const std::string& name)
    {
        for (const auto& substitution : substitutions)
            if (substitution.name == name)
                return &substitution;

        return nullptr;
    }
}

TEST ("offers a tritone substitution for a dominant chord")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");
    const auto substitutions = reharmonizer.substitutionsFor (chart, 1);

    const auto* tritone = find (substitutions, "Tritone substitution");
    CHECK (tritone != nullptr);
    CHECK_EQ (tritone->replacementText(), std::string ("Db7#11"));
    CHECK (tritone->difficulty == SubstitutionDifficulty::advanced);
}

TEST ("a tritone substitution keeps the original guide tones")
{
    const auto original = ChordSymbol::parse ("G7");
    const auto substitute = ChordSymbol::parse ("Db7");

    CHECK (original.has_value() && substitute.has_value());

    // B and F appear in both chords - that is why the substitution works.
    for (const auto& guide : original->guideTones())
        CHECK (substitute->containsPitchClass (original->root() + guide.semitones));
}

TEST ("offers the related ii-7 in front of a dominant")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| C7 | Fmaj7 |");

    const auto substitutions = reharmonizer.substitutionsFor (chart, 0);
    const auto* related = find (substitutions, "Add the related ii-7");
    CHECK (related != nullptr);
    CHECK_EQ (related->replacementText(), std::string ("Gm7 C7"));
}

TEST ("offers diatonic substitutions for a major chord")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| Cmaj7 | Cmaj7 |");
    const auto substitutions = reharmonizer.substitutionsFor (chart, 0);

    CHECK_EQ (find (substitutions, "Diatonic substitution (iii-7)")->replacementText(), std::string ("Em7"));
    CHECK_EQ (find (substitutions, "Diatonic substitution (vi-7)")->replacementText(), std::string ("Am7"));
}

TEST ("offers a backdoor ii-V only into a major chord a fourth above")
{
    const Reharmonizer reharmonizer;

    const auto resolving = chartFrom ("| C7 | Fmaj7 |");
    const auto resolvingSubstitutions = reharmonizer.substitutionsFor (resolving, 0);
    const auto* backdoor = find (resolvingSubstitutions, "Backdoor ii-V");
    CHECK (backdoor != nullptr);
    CHECK_EQ (backdoor->replacementText(), std::string ("Bbm7 Eb7"));

    const auto notResolving = chartFrom ("| C7 | Dm7 |");
    const auto otherSubstitutions = reharmonizer.substitutionsFor (notResolving, 0);
    CHECK (find (otherSubstitutions, "Backdoor ii-V") == nullptr);
}

TEST ("hides advanced substitutions when the learner asks for safe ones only")
{
    Reharmonizer::Options safeOptions;
    safeOptions.includeAdvanced = false;
    const Reharmonizer safeOnly { safeOptions };
    const auto chart = chartFrom ("| G7 | Cmaj7 |");

    for (const auto& substitution : safeOnly.substitutionsFor (chart, 0))
        CHECK (substitution.difficulty == SubstitutionDifficulty::safe);
}

TEST ("filters by style preset")
{
    Reharmonizer::Options modalOptions;
    modalOptions.style = ReharmStyle::modal;
    const Reharmonizer modal { modalOptions };
    const auto chart = chartFrom ("| G7 | Cmaj7 |");
    const auto substitutions = modal.substitutionsFor (chart, 0);

    CHECK (! substitutions.empty());

    for (const auto& substitution : substitutions)
        CHECK (substitution.style == ReharmStyle::modal || substitution.style == ReharmStyle::common);
}

TEST ("options are grouped by family, nearest the original harmony first")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cmaj7 | Dm7 |"), 0);

    auto lastFamily = -1;

    for (const auto& substitution : substitutions)
    {
        CHECK (static_cast<int> (substitution.family) >= lastFamily);
        lastFamily = static_cast<int> (substitution.family);
    }
}

TEST ("within a family, safe options come before advanced ones")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| G7 | Cmaj7 |"), 0);

    for (std::size_t i = 1; i < substitutions.size(); ++i)
    {
        if (substitutions[i].family != substitutions[i - 1].family)
            continue;

        if (substitutions[i - 1].difficulty == SubstitutionDifficulty::advanced)
            CHECK (substitutions[i].difficulty == SubstitutionDifficulty::advanced);
    }
}

TEST ("applying a substitution rewrites that measure only")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");
    const auto substitutions = reharmonizer.substitutionsFor (chart, 1);
    const auto* tritone = find (substitutions, "Tritone substitution");

    const auto reharmonised = Reharmonizer::applySubstitution (chart, 1, *tritone);

    CHECK_EQ (reharmonised.toProgressionText(), std::string ("| Dm7 | Db7#11 | Cmaj7 |"));
}

TEST ("applying a two-chord substitution splits the bar")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| C7 | Fmaj7 |");
    const auto substitutions = reharmonizer.substitutionsFor (chart, 0);
    const auto* related = find (substitutions, "Add the related ii-7");

    const auto reharmonised = Reharmonizer::applySubstitution (chart, 0, *related);
    const auto& measure = reharmonised.measures.front();

    CHECK_EQ (measure.slots.size(), std::size_t (2));
    CHECK_EQ (measure.slots[0].beats, 2);
    CHECK_EQ (measure.slots[1].beats, 2);
}

TEST ("guide tones of a ii-V-I move by a semitone or stay put")
{
    const auto dm7 = *ChordSymbol::parse ("Dm7");
    const auto g7 = *ChordSymbol::parse ("G7");

    for (const auto& motion : guideToneMotion (dm7, g7))
        CHECK (std::abs (motion.semitones) <= 1);

    // The classic voice leading: 7th of ii falls a semitone to the 3rd of V.
    CHECK_EQ (voiceLeadingCost (dm7, g7), 1);
}

TEST ("a measure index outside the chart yields nothing")
{
    const Reharmonizer reharmonizer;
    const auto chart = chartFrom ("| Dm7 |");

    CHECK (reharmonizer.substitutionsFor (chart, 5).empty());
    CHECK (reharmonizer.substitutionsFor (chart, -1).empty());
}


//==============================================================================
// Substitutions beyond the common vocabulary: chords borrowed from the parallel
// minor, chromatic mediants, passing chords and bass motion.

TEST ("offers the bVI major seventh over a major chord")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cmaj7 | Cmaj7 |"), 0);
    const auto* borrowed = find (substitutions, "bVI major seventh");

    CHECK (borrowed != nullptr);
    CHECK_EQ (borrowed->replacementText(), std::string ("Abmaj7"));
    CHECK (borrowed->family == SubstitutionFamily::modalInterchange);
    CHECK (borrowed->difficulty == SubstitutionDifficulty::advanced);
}

TEST ("the bVI substitution holds on to the original root and fifth")
{
    const auto original = *ChordSymbol::parse ("Cmaj7");
    const auto borrowed = *ChordSymbol::parse ("Abmaj7");

    // C and G are what let a chord from another key sit under the same melody.
    CHECK (borrowed.containsPitchClass (original.root()));
    CHECK (borrowed.containsPitchClass (original.root() + 7));
    CHECK (! borrowed.containsPitchClass (original.root() + 4));   // the 3rd goes
    CHECK (! borrowed.containsPitchClass (original.root() + 11));  // so does the 7th
}

TEST ("explanations name the notes the substitution keeps")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cmaj7 | Cmaj7 |"), 0);
    const auto* borrowed = find (substitutions, "bVI major seventh");

    CHECK (borrowed != nullptr);
    CHECK (borrowed->explanation.find ("keeps C and G") != std::string::npos);
}

TEST ("offers the other borrowed and mediant major chords")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cmaj7 | Cmaj7 |"), 0);

    CHECK_EQ (find (substitutions, "bIII major seventh")->replacementText(), std::string ("Ebmaj7"));
    CHECK_EQ (find (substitutions, "bVII approach")->replacementText(), std::string ("Bbmaj7 Cmaj7"));
    CHECK_EQ (find (substitutions, "Minor plagal approach")->replacementText(), std::string ("Fm6 Cmaj7"));

    const auto* mediant = find (substitutions, "III major seventh");
    CHECK_EQ (mediant->replacementText(), std::string ("Emaj7"));
    CHECK (mediant->family == SubstitutionFamily::chromaticMediant);
}

TEST ("the chromatic mediant keeps both guide tones of the original")
{
    const auto original = *ChordSymbol::parse ("Cmaj7");
    const auto mediant = *ChordSymbol::parse ("Emaj7");

    for (const auto& guide : original.guideTones())
        CHECK (mediant.containsPitchClass (original.root() + guide.semitones));
}

TEST ("offers modal colours for a minor chord")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Dm7 | G7 |"), 0);

    CHECK_EQ (find (substitutions, "Dorian 6th")->replacementText(), std::string ("Dm6"));
    CHECK_EQ (find (substitutions, "Minor-major seventh")->replacementText(), std::string ("DmMaj7"));
    CHECK_EQ (find (substitutions, "Darken it to m7b5")->replacementText(), std::string ("Dm7b5"));
}

TEST ("a diminished passing chord appears only when the roots move a whole tone")
{
    const Reharmonizer reharmonizer;

    const auto stepwise = chartFrom ("| Cmaj7 | Dm7 |");
    const auto stepwiseOptions = reharmonizer.substitutionsFor (stepwise, 0);
    const auto* passing = find (stepwiseOptions, "Diminished passing chord");

    CHECK (passing != nullptr);

    // Rising into Dm7, so it is spelled with a sharp rather than as Dbdim7.
    CHECK_EQ (passing->replacementText(), std::string ("Cmaj7 C#dim7"));

    const auto leap = chartFrom ("| Cmaj7 | Fmaj7 |");
    const auto leapOptions = reharmonizer.substitutionsFor (leap, 0);
    CHECK (find (leapOptions, "Diminished passing chord") == nullptr);
}

TEST ("the diminished passing chord shares four notes with the secondary dominant")
{
    const auto passing = *ChordSymbol::parse ("C#dim7");
    const auto secondary = *ChordSymbol::parse ("A7b9");

    auto shared = 0;

    for (auto pitchClass = 0; pitchClass < 12; ++pitchClass)
        if (passing.containsPitchClass (pitchClass) && secondary.containsPitchClass (pitchClass))
            ++shared;

    CHECK_EQ (shared, 4);
}

TEST ("offers bass motion without changing the harmony")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cmaj7 | Dm7 |"), 0);

    const auto* inversion = find (substitutions, "Third in the bass");
    CHECK_EQ (inversion->replacementText(), std::string ("Cmaj7/E"));
    CHECK (inversion->family == SubstitutionFamily::bassMotion);
    CHECK (inversion->difficulty == SubstitutionDifficulty::safe);

    const auto* pedal = find (substitutions, "Triad over a tonic pedal");
    CHECK_EQ (pedal->replacementText(), std::string ("D/C"));
    CHECK_EQ (*pedal->replacement.front().bass(), 0);
}

TEST ("a triad over the tonic pedal spells the Lydian tensions")
{
    const auto pedal = *ChordSymbol::parse ("D/C");

    CHECK (pedal.containsPitchClass (2));   // 9th
    CHECK (pedal.containsPitchClass (6));   // #11
    CHECK (pedal.containsPitchClass (9));   // 13th
    CHECK (pedal.containsPitchClass (0));   // the pedal itself
}

TEST ("every generated substitution can be read back as a chord symbol")
{
    const Reharmonizer reharmonizer;

    for (const auto& text : { "| Cmaj7 | Dm7 |", "| Dm7 | G7 |", "| G7 | Cmaj7 |",
                              "| Bbmaj7 | Eb7 |", "| F#m7b5 | B7alt |", "| Ab13 | Dbmaj7 |" })
    {
        const auto chart = chartFrom (text);

        for (auto measure = 0; measure < chart.measureCount(); ++measure)
        {
            for (const auto& substitution : reharmonizer.substitutionsFor (chart, measure))
            {
                CHECK (! substitution.replacement.empty());
                CHECK (! substitution.explanation.empty());

                for (const auto& chord : substitution.replacement)
                {
                    const auto reparsed = ChordSymbol::parse (chord.toString());
                    CHECK (reparsed.has_value());
                    CHECK (*reparsed == chord);
                }
            }
        }
    }
}

TEST ("a chord not in the common vocabulary still gets options")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| Cdim7 | Dm7 |"), 0);

    // Nothing quality-specific fires for a diminished chord, but the rules that
    // depend only on the next chord, and bass motion, still apply.
    CHECK (! substitutions.empty());
    CHECK (find (substitutions, "Third in the bass") != nullptr);
}

TEST ("the modal style preset keeps the borrowed chords")
{
    Reharmonizer::Options modalOptions;
    modalOptions.style = ReharmStyle::modal;
    const Reharmonizer modal { modalOptions };
    const auto substitutions = modal.substitutionsFor (chartFrom ("| Cmaj7 | Dm7 |"), 0);

    CHECK (find (substitutions, "bVI major seventh") != nullptr);
    CHECK (find (substitutions, "Tritone substitution") == nullptr);
}

//==============================================================================
// Spotting a reharmonisation the player has found by ear.

namespace
{
    std::optional<RecognisedSubstitution> recognise (const std::string& progression,
                                                     int measureIndex,
                                                     std::vector<int> notes,
                                                     Reharmonizer::Options options = {})
    {
        return recogniseSubstitution (Voicing::fromNotes (std::move (notes)),
                                      chartFrom (progression), measureIndex, options);
    }
}

TEST ("spots the bVI substitution when it is played over the written chord")
{
    // Ab C Eb G over a bar of Cmaj7: the player has found Abmaj7.
    const auto found = recognise ("| Cmaj7 | Dm7 |", 0, { 56, 60, 63, 67 });

    CHECK (found.has_value());
    CHECK_EQ (found->substitution.name, std::string ("bVI major seventh"));
    CHECK_EQ (found->chord.toString(), std::string ("Abmaj7"));
    CHECK (found->score > found->writtenChordScore);
}

TEST ("says nothing when the voicing is simply the written chord")
{
    CHECK (! recognise ("| Cmaj7 | Dm7 |", 0, { 60, 64, 67, 71 }).has_value());
    CHECK (! recognise ("| Dm7 | G7 |", 0, { 53, 57, 60, 62 }).has_value());
}

TEST ("says nothing about two notes")
{
    CHECK (! recognise ("| Cmaj7 | Dm7 |", 0, { 56, 60 }).has_value());
}

TEST ("spots a diatonic substitution")
{
    // A C E G over Cmaj7 is the relative minor, not a broken Cmaj7.
    const auto found = recognise ("| Cmaj7 | Dm7 |", 0, { 57, 60, 64, 67 });

    CHECK (found.has_value());
    CHECK_EQ (found->chord.toString(), std::string ("Am7"));
    CHECK (found->substitution.family == SubstitutionFamily::diatonic);
}

TEST ("spots an altered dominant")
{
    // B Db Eb F Ab Bb over G7: the 3rd and b7 with every tension altered.
    const auto found = recognise ("| G7 | Cmaj7 |", 0, { 59, 61, 63, 65, 68, 70 });

    CHECK (found.has_value());
    CHECK_EQ (found->substitution.name, std::string ("Altered dominant"));
}

TEST ("spots half of a two-chord substitution")
{
    // F Ab C D over a Cmaj7 bar is the Fm6 that opens the minor plagal approach.
    const auto found = recognise ("| Cmaj7 | Dm7 |", 0, { 53, 56, 60, 62 });

    CHECK (found.has_value());
    CHECK_EQ (found->chord.toString(), std::string ("Fm6"));
    CHECK_EQ (found->substitution.name, std::string ("Minor plagal approach"));
}

TEST ("when two substitutions spell the same notes, the nearer one is named")
{
    // D F A C over a G7 bar is a rootless G9sus4 and a Dm7 at the same time -
    // both are offered for that bar. The reading closest to the written chord
    // wins, so the player is told they suspended the dominant rather than that
    // they played a different chord.
    const auto found = recognise ("| G7 | Cmaj7 |", 0, { 50, 57, 60, 65 });

    CHECK (found.has_value());
    CHECK_EQ (found->substitution.name, std::string ("Suspend the dominant"));
    CHECK (found->substitution.family == SubstitutionFamily::extension);
}

TEST ("keeps quiet about advanced substitutions when the learner asked for safe ones")
{
    Reharmonizer::Options safeOnly;
    safeOnly.includeAdvanced = false;

    CHECK (recognise ("| Cmaj7 | Dm7 |", 0, { 56, 60, 63, 67 }).has_value());
    CHECK (! recognise ("| Cmaj7 | Dm7 |", 0, { 56, 60, 63, 67 }, safeOnly).has_value());
}

TEST ("a spotted substitution can be applied to the chart it was spotted in")
{
    const auto chart = chartFrom ("| Cmaj7 | Dm7 |");
    const auto found = recogniseSubstitution (Voicing::fromNotes ({ 56, 60, 63, 67 }), chart, 0);

    CHECK (found.has_value());

    const auto reharmonised = Reharmonizer::applySubstitution (chart, 0, found->substitution);
    CHECK_EQ (reharmonised.toProgressionText(), std::string ("| Abmaj7 | Dm7 |"));
}

TEST ("a voicing that fits nothing in particular is not forced into a substitution")
{
    // A cluster with no clear reading: C Db D Eb.
    CHECK (! recognise ("| Cmaj7 | Dm7 |", 0, { 60, 61, 62, 63 }).has_value());
}

//==============================================================================
// Reharmonising a whole tune rather than one bar.

namespace
{
    const char* practiceChart = "| Dm7 | G7 | Cmaj7 | Cmaj7 | Cm7 | F7 "
                                "| Bbmaj7 | Bbmaj7 | Am7b5 | D7alt | Gm7 | Gm7 |";

    std::string measureTextAt (const Chart& chart, int index)
    {
        std::string text;

        for (const auto& slot : chart.measures[static_cast<std::size_t> (index)].slots)
            text += (text.empty() ? "" : " ") + slot.chord.toString();

        return text;
    }
}

TEST ("every plan returns a tune of the same length")
{
    const auto chart = chartFrom (practiceChart);

    for (const auto& plan : reharmPlansFor (chart))
    {
        CHECK_EQ (plan.chart.measureCount(), chart.measureCount());
        CHECK (! plan.name.empty());
        CHECK (! plan.description.empty());
    }
}

TEST ("the plans are offered lightest touch first")
{
    const auto plans = reharmPlansFor (chartFrom (practiceChart));

    CHECK_EQ (plans.size(), std::size_t (6));
    CHECK_EQ (plans.front().name, std::string ("Minimal touch"));
    CHECK_EQ (plans.back().name, std::string ("Out there"));
    CHECK (plans[0].barsChanged() < plans[4].barsChanged());   // minimal vs adventurous
}

TEST ("the minimal plan only rewrites bars that were repeating the one before")
{
    const auto chart = chartFrom (practiceChart);
    const auto plan = makeReharmPlan (chart, ReharmPlanKind::minimalTouch);

    CHECK (plan.barsChanged() > 0);

    for (const auto& move : plan.moves)
    {
        CHECK (move.measureIndex > 0);
        CHECK_EQ (measureTextAt (chart, move.measureIndex),
                  measureTextAt (chart, move.measureIndex - 1));
    }
}

TEST ("the recommended plan never changes two bars in a row")
{
    const auto plan = makeReharmPlan (chartFrom (practiceChart), ReharmPlanKind::recommended);

    for (std::size_t i = 1; i < plan.moves.size(); ++i)
        CHECK (plan.moves[i].measureIndex - plan.moves[i - 1].measureIndex > 1);
}

TEST ("plans that keep the final bar leave the tune where it landed")
{
    const auto chart = chartFrom (practiceChart);
    const auto lastBar = chart.measureCount() - 1;

    for (auto kind : { ReharmPlanKind::minimalTouch, ReharmPlanKind::recommended,
                       ReharmPlanKind::modalColour })
    {
        const auto plan = makeReharmPlan (chart, kind);
        CHECK_EQ (measureTextAt (plan.chart, lastBar), measureTextAt (chart, lastBar));
    }
}

TEST ("no plan rewrites a bar into the bar before it")
{
    for (auto kind : { ReharmPlanKind::minimalTouch, ReharmPlanKind::recommended,
                       ReharmPlanKind::modalColour, ReharmPlanKind::cycleOfFifths,
                       ReharmPlanKind::adventurous })
    {
        const auto plan = makeReharmPlan (chartFrom (practiceChart), kind);

        for (const auto& move : plan.moves)
            if (move.measureIndex > 0)
                CHECK (move.after != measureTextAt (plan.chart, move.measureIndex - 1));
    }
}

TEST ("what a plan says it did is what is in the chart")
{
    const auto plan = makeReharmPlan (chartFrom (practiceChart), ReharmPlanKind::adventurous);

    CHECK (plan.barsChanged() > 0);

    for (const auto& move : plan.moves)
    {
        CHECK_EQ (measureTextAt (plan.chart, move.measureIndex), move.after);
        CHECK (move.before != move.after);
        CHECK (! move.substitution.empty());
    }
}

TEST ("every chord a plan writes can be read back")
{
    for (const auto& plan : reharmPlansFor (chartFrom (practiceChart)))
        for (const auto& measure : plan.chart.measures)
            for (const auto& slot : measure.slots)
            {
                const auto reparsed = ChordSymbol::parse (slot.chord.toString());
                CHECK (reparsed.has_value());
                CHECK (*reparsed == slot.chord);
            }
}

TEST ("planning the same tune twice gives the same tune")
{
    const auto chart = chartFrom (practiceChart);

    for (auto kind : { ReharmPlanKind::recommended, ReharmPlanKind::cycleOfFifths,
                       ReharmPlanKind::adventurous })
        CHECK_EQ (makeReharmPlan (chart, kind).chart.toProgressionText(),
                  makeReharmPlan (chart, kind).chart.toProgressionText());
}

TEST ("a plan can be made of a one-bar tune without trouble")
{
    const auto chart = chartFrom ("| Cmaj7 |");

    for (const auto& plan : reharmPlansFor (chart))
    {
        CHECK_EQ (plan.chart.measureCount(), 1);

        // Every plan but the two that play through the final bar leaves it alone.
        if (plan.kind != ReharmPlanKind::adventurous && plan.kind != ReharmPlanKind::cycleOfFifths)
            CHECK_EQ (plan.barsChanged(), 0);
    }
}

TEST ("the cycle plan puts dominants in front of things")
{
    const auto plan = makeReharmPlan (chartFrom (practiceChart), ReharmPlanKind::cycleOfFifths);

    CHECK (plan.barsChanged() >= 6);

    auto barsWithTwoChords = 0;

    for (const auto& measure : plan.chart.measures)
        if (measure.slots.size() > 1)
            ++barsWithTwoChords;

    CHECK (barsWithTwoChords >= 5);
}

TEST ("the modal plan borrows rather than re-routes")
{
    const auto plan = makeReharmPlan (chartFrom (practiceChart), ReharmPlanKind::modalColour);

    CHECK (plan.barsChanged() > 0);

    for (const auto& move : plan.moves)
        CHECK (move.family == SubstitutionFamily::modalInterchange
               || move.family == SubstitutionFamily::chromaticMediant
               || move.family == SubstitutionFamily::extension);
}

//==============================================================================
// The substitutions that only work sometimes, and the verdict that says when.

namespace
{
    Reharmonizer riskyReharmonizer()
    {
        Reharmonizer::Options options;
        options.includeRisky = true;
        return Reharmonizer { options };
    }
}

TEST ("risky substitutions are off unless asked for")
{
    const Reharmonizer standard;
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");

    for (const auto& substitution : standard.substitutionsFor (chart, 1))
        CHECK (substitution.difficulty != SubstitutionDifficulty::risky);

    auto foundRisky = false;

    for (const auto& substitution : riskyReharmonizer().substitutionsFor (chart, 1))
        foundRisky = foundRisky || substitution.difficulty == SubstitutionDifficulty::risky;

    CHECK (foundRisky);
}

TEST ("the tritone major seventh is offered for a dominant")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 |");
    const auto substitutions = riskyReharmonizer().substitutionsFor (chart, 1);
    const auto* tritoneMajor = find (substitutions, "Tritone major seventh");

    CHECK (tritoneMajor != nullptr);
    CHECK_EQ (tritoneMajor->replacementText(), std::string ("Dbmaj7"));
    CHECK (tritoneMajor->difficulty == SubstitutionDifficulty::risky);
}

TEST ("G7 to Dbmaj7 is judged to work in a ii-V-I")
{
    // The guide tones of Dm7 are already the guide tones of Dbmaj7, and they
    // fall a semitone each into Cmaj7 - which is the whole reason it works.
    const auto substitutions = riskyReharmonizer().substitutionsFor (chartFrom ("| Dm7 | G7 | Cmaj7 |"), 1);
    const auto& verdict = find (substitutions, "Tritone major seventh")->voiceLeading;

    CHECK (verdict.smoothHere);
    CHECK_EQ (verdict.approachCost, 0);
    CHECK_EQ (verdict.departureCost, 2);
    CHECK (verdict.note.find ("Works here") != std::string::npos);
    CHECK (verdict.note.find ("Cmaj7") != std::string::npos);
}

TEST ("the same substitution is judged on the bar it is offered for")
{
    // Nothing about Dbmaj7 changes; what changes is what is on either side.
    const auto smooth = riskyReharmonizer().substitutionsFor (chartFrom ("| Dm7 | G7 | Cmaj7 |"), 1);
    const auto rough = riskyReharmonizer().substitutionsFor (chartFrom ("| Ebmaj7 | G7 | F#m7 |"), 1);

    CHECK (find (smooth, "Tritone major seventh")->voiceLeading.smoothHere);
    CHECK (! find (rough, "Tritone major seventh")->voiceLeading.smoothHere);
}

TEST ("a verdict blames the side of the bar that is actually at fault")
{
    // Ab/G leaves for Cmaj7 cleanly but is a scramble to arrive at from Dm7.
    const auto substitutions = riskyReharmonizer().substitutionsFor (chartFrom ("| Dm7 | G7 | Cmaj7 |"), 1);
    const auto& verdict = find (substitutions, "Upper-structure triad")->voiceLeading;

    CHECK (! verdict.smoothHere);
    CHECK (verdict.approachCost > verdict.departureCost);
    CHECK (verdict.note.find ("getting into it from Dm7") != std::string::npos);
}

TEST ("every risky substitution arrives with a verdict")
{
    for (const auto& text : { "| Dm7 | G7 | Cmaj7 |", "| Cmaj7 | Am7 |", "| Fm7 | Bb7 | Ebmaj7 |" })
    {
        const auto chart = chartFrom (text);

        for (auto measure = 0; measure < chart.measureCount(); ++measure)
            for (const auto& substitution : riskyReharmonizer().substitutionsFor (chart, measure))
                if (substitution.difficulty == SubstitutionDifficulty::risky)
                {
                    CHECK (! substitution.voiceLeading.note.empty());
                    CHECK (substitution.voiceLeading.note.find ("here") != std::string::npos);
                }
    }
}

TEST ("the hexatonic pole shares nothing with the triad, and only the 7th with the seventh chord")
{
    const auto substitutions = riskyReharmonizer().substitutionsFor (chartFrom ("| Cmaj7 | Am7 |"), 0);
    const auto* pole = find (substitutions, "Hexatonic pole");

    CHECK (pole != nullptr);
    CHECK_EQ (pole->replacementText(), std::string ("Abm"));

    const auto triad = *ChordSymbol::parse ("C");
    const auto poleChord = *ChordSymbol::parse ("Abm");

    // Against the plain triad the two chords have nothing in common at all.
    for (auto pitchClass = 0; pitchClass < 12; ++pitchClass)
        CHECK (! (triad.containsPitchClass (pitchClass) && poleChord.containsPitchClass (pitchClass)));

    // Against Cmaj7 they share exactly one note - the major 7th, spelled Cb in
    // Ab minor, which is the pivot that makes the move followable.
    CHECK_EQ (pole->voiceLeading.sharedWithOriginal, 1);
    CHECK (poleChord.containsPitchClass (11));
}

TEST ("risky substitutions are listed after the advanced ones in their family")
{
    const auto substitutions = riskyReharmonizer().substitutionsFor (chartFrom ("| Dm7 | G7 | Cmaj7 |"), 1);

    for (std::size_t i = 1; i < substitutions.size(); ++i)
    {
        if (substitutions[i].family != substitutions[i - 1].family)
            continue;

        if (substitutions[i - 1].difficulty == SubstitutionDifficulty::risky)
            CHECK (substitutions[i].difficulty == SubstitutionDifficulty::risky);
    }
}

TEST ("every risky substitution can still be read back as a chord")
{
    const auto chart = chartFrom ("| Dm7 | G7 | Cmaj7 | Am7b5 | D7alt | Gm6 |");

    for (auto measure = 0; measure < chart.measureCount(); ++measure)
        for (const auto& substitution : riskyReharmonizer().substitutionsFor (chart, measure))
            for (const auto& replacement : substitution.replacement)
            {
                const auto reparsed = ChordSymbol::parse (replacement.toString());
                CHECK (reparsed.has_value());
                CHECK (*reparsed == replacement);
            }
}

TEST ("the out-there plan only takes risks that land")
{
    const auto chart = chartFrom (practiceChart);
    const auto plan = makeReharmPlan (chart, ReharmPlanKind::outThere);

    CHECK (plan.barsChanged() > 0);
    CHECK (plan.barsChanged() < chart.measureCount());   // some bars have nothing that works

    const auto reharmonizer = riskyReharmonizer();

    for (const auto& move : plan.moves)
    {
        // Whatever it chose for a bar was a risky move the engine judged smooth.
        const auto options = reharmonizer.substitutionsFor (chart, move.measureIndex);
        const auto* chosen = find (options, move.substitution);

        if (chosen != nullptr)
            CHECK (chosen->difficulty == SubstitutionDifficulty::risky);
    }
}

TEST ("the out-there plan leaves a bar alone rather than forcing a risk")
{
    // A single bar with nothing after it: no risk can be judged to land.
    const auto plan = makeReharmPlan (chartFrom ("| Cmaj7 |"), ReharmPlanKind::outThere);
    CHECK_EQ (plan.barsChanged(), 0);
}

TEST ("difficulty names cover the risky tier")
{
    CHECK_EQ (difficultyName (SubstitutionDifficulty::safe), std::string ("Safe"));
    CHECK_EQ (difficultyName (SubstitutionDifficulty::advanced), std::string ("Advanced"));
    CHECK_EQ (difficultyName (SubstitutionDifficulty::risky), std::string ("Risky"));
}
