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
    const Reharmonizer safeOnly { Reharmonizer::Options { false, ReharmStyle::common } };
    const auto chart = chartFrom ("| G7 | Cmaj7 |");

    for (const auto& substitution : safeOnly.substitutionsFor (chart, 0))
        CHECK (substitution.difficulty == SubstitutionDifficulty::safe);
}

TEST ("filters by style preset")
{
    const Reharmonizer modal { Reharmonizer::Options { true, ReharmStyle::modal } };
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
    const Reharmonizer modal { Reharmonizer::Options { true, ReharmStyle::modal } };
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
    const Reharmonizer::Options safeOnly { false, ReharmStyle::common };

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
