#include "TestFramework.h"
#include "jazz/core/Reharmonizer.h"

#include <cstdlib>

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

TEST ("safe substitutions are listed before advanced ones")
{
    const Reharmonizer reharmonizer;
    const auto substitutions = reharmonizer.substitutionsFor (chartFrom ("| G7 | Cmaj7 |"), 0);

    auto seenAdvanced = false;

    for (const auto& substitution : substitutions)
    {
        if (substitution.difficulty == SubstitutionDifficulty::advanced)
            seenAdvanced = true;
        else
            CHECK (! seenAdvanced);
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
