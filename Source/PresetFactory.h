#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>

class PresetFactory
{
public:
    static void installDefaultPresets(const juce::File& presetDirectory)
    {
        if (presetDirectory.findChildFiles(juce::File::findFiles, false, "*.xml").size() > 0) return;

        auto createPreset = [&](const juce::String& name, std::map<juce::String, float> params, std::vector<int> activePedals)
            {
                juce::XmlElement xmlState("Parameters");
                for (auto const& [paramID, value] : params) {
                    auto* param = new juce::XmlElement("PARAM");
                    param->setAttribute("id", paramID);
                    param->setAttribute("value", value);
                    xmlState.addChildElement(param);
                }
                for (int pedalID : activePedals) {
                    auto* param = new juce::XmlElement("PARAM");
                    param->setAttribute("id", "byp_" + juce::String(pedalID));
                    param->setAttribute("value", 0.0f);
                    xmlState.addChildElement(param);
                }
                auto* routingXml = new juce::XmlElement("ROUTING");
                for (int i = 0; i < 20; ++i) routingXml->setAttribute("slot" + juce::String(i), i);
                xmlState.addChildElement(routingXml);
                xmlState.writeTo(presetDirectory.getChildFile(name + ".xml"));
            };

        // ==========================================================
        // 0:Gate, 1:Comp, 2:Boost, 3:Dist, 4:EQ, 5:Pitch, 6:Oct, 7:Cab, 
        // 8:Wah, 9:Phas, 10:Flng, 11:Trem, 12:Cho, 13:Dly, 14:Rvb, 
        // 15:Acoust, 16:Synth, 17:Loop, 18:Crush, 19:Ring
        // ==========================================================

        // --- STEVE VAI ---
        createPreset("Steve Vai - For the Love of God", { {"ng_thresh", -40.f}, {"drive", 65.f}, {"dist_type", 1.f}, {"eq_mid", 4.f}, {"eq_high", 2.f}, {"delay_time", 0.35f}, {"delay_feed", 0.4f}, {"delay_mix", 0.3f}, {"rvb_room", 0.6f}, {"rvb_mix", 0.25f} }, { 0, 3, 4, 7, 13, 14 });
        //createPreset("Steve Vai - Bad Horsie", { {"drive", 75.f}, {"dist_type", 0.f}, {"wah_rate", 2.5f}, {"wah_depth", 0.9f}, {"wah_q", 8.f}, {"eq_low", 4.f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 3, 4, 7, 8, 13 });
        //createPreset("Steve Vai - Tender Surrender (Clean)", { {"cmp_thresh", -30.f}, {"cmp_ratio", 4.f}, {"cho_rate", 1.5f}, {"cho_depth", 0.4f}, {"cho_mix", 0.5f}, {"delay_time", 0.3f}, {"delay_mix", 0.2f}, {"rvb_room", 0.5f}, {"rvb_mix", 0.3f} }, { 1, 4, 7, 12, 13, 14 });
        //createPreset("Steve Vai - Tender Surrender (Dist)", { {"drive", 55.f}, {"dist_type", 1.f}, {"eq_mid", 6.f}, {"eq_high", -2.f}, {"delay_time", 0.4f}, {"delay_mix", 0.25f}, {"rvb_room", 0.5f}, {"rvb_mix", 0.2f} }, { 3, 4, 7, 13, 14 });
        createPreset("Steve Vai - Fire Garden", { {"drive", 90.f}, {"dist_type", 2.f}, {"phs_rate", 1.2f}, {"phs_depth", 0.6f}, {"delay_time", 0.5f}, {"delay_mix", 0.3f} }, { 3, 4, 7, 9, 13 });

        // --- EVH ---
        createPreset("EVH - Eruption", { {"bst_gain", 15.f}, {"drive", 100.f}, {"dist_type", 0.f}, {"phs_rate", 0.8f}, {"phs_depth", 0.7f}, {"phs_feed", 0.4f}, {"eq_low", 2.f}, {"eq_high", 4.f}, {"delay_time", 0.2f}, {"delay_feed", 0.2f}, {"delay_mix", 0.15f}, {"rvb_room", 0.8f}, {"rvb_mix", 0.3f}}, {2, 3, 4, 7, 9, 13, 14});
        createPreset("EVH - Panama", { {"bst_gain", 15.f}, {"drive", 100.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", 2.f}, {"eq_high", 5.f}, {"rvb_room", 0.6f}, {"rvb_mix", 0.2f} }, { 2, 3, 4, 7, 14 });
        createPreset("EVH - Unchained", { {"bst_gain", 15.f}, {"drive", 100.f}, {"dist_type", 0.f}, {"flg_rate", 0.3f}, {"flg_depth", 0.8f}, {"flg_feed", 0.5f}, {"eq_low", 4.f}, {"eq_high", 5.f} }, { 2, 3, 4, 7, 10 });
        //createPreset("EVH - Spanish Fly", { {"ac_body", 6.f}, {"ac_air", 8.f}, {"ac_reso", 0.5f}, {"rvb_room", 0.7f}, {"rvb_mix", 0.4f} }, { 14, 15 });
        createPreset("EVH - Mean Street", { {"bst_gain", 20.f}, {"drive", 100.f}, {"dist_type", 0.f}, {"flg_rate", 0.2f}, {"flg_depth", 0.9f}, {"flg_feed", 0.6f} }, { 2, 3, 4, 7, 10 });
        //createPreset("EVH - Cathedral", { {"drive", 40.f}, {"dist_type", 1.f}, {"cho_rate", 2.f}, {"cho_mix", 0.4f}, {"delay_time", 0.4f}, {"delay_feed", 0.4f}, {"delay_mix", 0.6f} }, { 3, 7, 12, 13 });

        // --- YNGWIE MALMSTEEN ---
        createPreset("Malmsteen - Far Beyond the Sun", { {"ng_thresh", -30.f}, {"bst_gain", 12.f}, {"drive", 70.f}, {"dist_type", 0.f}, {"eq_low", -3.f}, {"eq_mid", 5.f}, {"eq_high", 6.f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 0, 2, 3, 4, 7, 13 });
        createPreset("Malmsteen - Trilogy Suite Op. 5", { {"ng_thresh", -35.f}, {"bst_gain", 14.f}, {"drive", 65.f}, {"dist_type", 0.f}, {"eq_mid", 6.f}, {"eq_high", 5.f}, {"delay_time", 0.35f}, {"delay_mix", 0.15f} }, { 0, 2, 3, 4, 7, 13 });
        createPreset("Malmsteen - Black Star", { {"drive", 55.f}, {"dist_type", 1.f}, {"eq_low", -2.f}, {"eq_mid", 7.f}, {"eq_high", 4.f}, {"delay_time", 0.45f}, {"delay_mix", 0.25f}, {"rvb_room", 0.5f}, {"rvb_mix", 0.2f} }, { 3, 4, 7, 13, 14 });
        createPreset("Malmsteen - Odyssey", { {"bst_gain", 10.f}, {"drive", 75.f}, {"dist_type", 0.f}, {"eq_mid", 4.f}, {"cho_rate", 1.f}, {"cho_mix", 0.3f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 2, 3, 4, 7, 12, 13 });

        // --- MEGADETH ---
        createPreset("Megadeth - Rust in Peace", { {"drive", 100.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 2.f}, {"eq_high", 7.f} }, { 3, 4, 7 });
        createPreset("Megadeth - Youthanasia", { {"ng_thresh", -30.f}, {"drive", 90.f}, {"dist_type", 0.f}, {"eq_low", 6.f}, {"eq_mid", 3.f}, {"eq_high", 5.f} }, { 0, 3, 4, 7 });
        createPreset("Megadeth - Peace Sells", { {"ng_thresh", -20.f}, {"drive", 80.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", -4.f}, {"eq_high", 8.f} }, { 0, 3, 4, 7 });
        createPreset("Megadeth - Dystopia", { {"ng_thresh", -15.f}, {"drive", 95.f}, {"dist_type", 2.f}, {"eq_low", 7.f}, {"eq_mid", -2.f}, {"eq_high", 6.f} }, { 0, 3, 4, 7 });

        // --- METALLICA ---
        createPreset("Metallica - Master of Puppets", { {"ng_thresh", -30.f}, {"ng_rel", 20.f}, {"drive", 90.f}, {"dist_type", 2.f}, {"eq_low", 6.f}, {"eq_mid", -8.f}, {"eq_high", 7.f}, {"rvb_room", 0.3f}, {"rvb_mix", 0.1f} }, { 0, 3, 4, 7, 14 });
        createPreset("Metallica - Ride the Lightning", { {"drive", 85.f}, {"dist_type", 2.f}, {"eq_low", 5.f}, {"eq_mid", -10.f}, {"eq_high", 8.f}, {"delay_time", 0.15f}, {"delay_mix", 0.1f} }, { 3, 4, 7, 13 });
        createPreset("Metallica - And Justice For All", { {"ng_thresh", -30.f}, {"drive", 95.f}, {"dist_type", 2.f}, {"eq_low", 8.f}, {"eq_mid", -12.f}, {"eq_high", 9.f} }, { 0, 3, 4, 7 });
        createPreset("Metallica - Black Album", { {"ng_thresh", -25.f}, {"drive", 95.f}, {"dist_type", 0.f}, {"eq_low", 7.f}, {"eq_mid", -4.f}, {"eq_high", 5.f}, {"rvb_room", 0.4f}, {"rvb_mix", 0.15f} }, { 0, 3, 4, 7, 14 });
        createPreset("Metallica - Kill 'Em All", { {"drive", 80.f}, {"dist_type", 0.f}, {"eq_low", 2.f}, {"eq_mid", 4.f}, {"eq_high", 7.f} }, { 3, 4, 7 });

        // --- IRON MAIDEN ---
        createPreset("Iron Maiden - The Number of the Beast", { {"drive", 70.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 6.f}, {"eq_high", 5.f} }, { 3, 4, 7 });
        createPreset("Iron Maiden - Powerslave", { {"drive", 75.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", 5.f}, {"eq_high", 6.f}, {"delay_time", 0.3f}, {"delay_mix", 0.15f} }, { 3, 4, 7, 13 });
        createPreset("Iron Maiden - Somewhere in Time", { {"drive", 75.f}, {"dist_type", 1.f}, {"eq_mid", 4.f}, {"cho_rate", 1.5f}, {"cho_mix", 0.5f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 3, 4, 7, 12, 13 });
        createPreset("Iron Maiden - Fear of the Dark", { {"drive", 80.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_mid", 4.f}, {"eq_high", 6.f} }, { 3, 4, 7 });
        createPreset("Iron Maiden - Piece of Mind", { {"drive", 70.f}, {"dist_type", 0.f}, {"eq_mid", 7.f}, {"eq_high", 5.f} }, { 3, 4, 7 });

        // --- OZZY / ZAKK / RANDY ---
        createPreset("Ozzy - Blizzard of Ozz", { {"drive", 87.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", 8.f}, {"eq_high", 6.f}, {"cho_rate", 0.5f}, {"cho_depth", 0.3f}, {"cho_mix", 0.3f} }, { 3, 4, 7, 12 });
        createPreset("Ozzy - Bark at the Moon", { {"drive", 100.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_mid", 4.f}, {"eq_high", 7.f}, {"delay_time", 0.2f}, {"delay_mix", 0.15f} }, { 3, 4, 7, 13 });
        createPreset("Ozzy - No More Tears", { {"drive", 90.f}, {"dist_type", 1.f}, {"eq_low", 6.f}, {"eq_mid", 2.f}, {"eq_high", 4.f}, {"cho_rate", 2.f}, {"cho_depth", 0.6f}, {"cho_mix", 0.5f} }, { 3, 4, 7, 12 });
        createPreset("Ozzy - Shot in the Dark", { {"drive", 80.f}, {"dist_type", 0.f}, {"eq_mid", 5.f}, {"cho_rate", 1.2f}, {"cho_mix", 0.4f} }, { 3, 4, 7, 12 });

        // --- BLACK SABBATH ---
        createPreset("Black Sabbath - Black Sabbath", { {"drive", 65.f}, {"dist_type", 2.f}, {"eq_low", 6.f}, {"eq_mid", 5.f}, {"eq_high", -2.f} }, { 3, 4, 7 });
        createPreset("Black Sabbath - Heaven & Hell", { {"drive", 75.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 6.f}, {"eq_high", 4.f}, {"cho_rate", 0.8f}, {"cho_mix", 0.2f} }, { 3, 4, 7, 12 });
        createPreset("Black Sabbath - Mob Rules", { {"drive", 80.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_mid", 7.f}, {"eq_high", 3.f} }, { 3, 4, 7 });

        // --- GNR & SLASH ---
        createPreset("GNR - Appetite For Destruction", { {"drive", 70.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 5.f}, {"eq_high", 6.f}, {"rvb_room", 0.4f}, {"rvb_mix", 0.1f} }, { 3, 4, 7, 14 });
        createPreset("GNR - Use Your Illusion", { {"drive", 75.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_mid", 4.f}, {"eq_high", 5.f}, {"cho_rate", 1.2f}, {"cho_mix", 0.2f}, {"delay_time", 0.35f}, {"delay_mix", 0.15f} }, { 3, 4, 7, 12, 13 });

        // --- PINK FLOYD ---
        createPreset("Pink Floyd - The Dark Side of the Moon", { {"drive", 30.f}, {"dist_type", 1.f}, {"phs_rate", 1.5f}, {"phs_depth", 0.8f}, {"phs_mix", 0.5f}, {"delay_time", 0.35f}, {"delay_feed", 0.4f}, {"delay_mix", 0.3f} }, { 3, 7, 9, 13 });
        createPreset("Pink Floyd - The Wall (Lead)", { {"drive", 75.f}, {"dist_type", 2.f}, {"cho_rate", 1.2f}, {"cho_mix", 0.3f}, {"delay_time", 0.48f}, {"delay_feed", 0.5f}, {"delay_mix", 0.45f}, {"rvb_room", 0.7f}, {"rvb_mix", 0.3f} }, { 3, 7, 12, 13, 14 });
        createPreset("Pink Floyd - Meddle", { {"drive", 35.f}, {"dist_type", 2.f}, {"eq_high", 4.f}, {"delay_time", 0.3f}, {"delay_feed", 0.6f}, {"delay_mix", 0.5f} }, { 3, 4, 7, 13 });
        //createPreset("Pink Floyd - Animals", { {"cmp_thresh", -30.f}, {"phs_rate", 0.8f}, {"phs_mix", 0.4f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 1, 7, 9, 13 });
        createPreset("Pink Floyd - The Division Bell", { {"cmp_thresh", -25.f}, {"drive", 45.f}, {"dist_type", 1.f}, {"cho_rate", 2.f}, {"cho_depth", 0.6f}, {"cho_mix", 0.3f}, {"delay_time", 0.5f}, {"delay_mix", 0.3f} }, { 1, 3, 7, 12, 13 });

        // --- HARD ROCK & METAL LEGENDS ---
        createPreset("Rainbow - Rising", { {"drive", 85.f}, {"dist_type", 0.f}, {"eq_high", 7.f}, {"delay_time", 0.2f}, {"delay_mix", 0.1f} }, { 3, 4, 7, 13 });
        createPreset("Dio - Holy Diver", { {"drive", 85.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 5.f}, {"eq_high", 6.f} }, { 3, 4, 7 });
        createPreset("Whitesnake - 1987", { {"drive", 80.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_high", 6.f}, {"cho_rate", 1.8f}, {"cho_mix", 0.2f}, {"delay_time", 0.45f}, {"delay_mix", 0.25f}, {"rvb_room", 0.7f}, {"rvb_mix", 0.3f} }, { 3, 4, 7, 12, 13, 14 });
        createPreset("Brian May", { {"bst_gain", 18.f}, {"drive", 50.f}, {"dist_type", 0.f}, {"eq_low", -4.f}, {"eq_mid", 8.f}, {"eq_high", 5.f}, {"cho_rate", 0.5f}, {"cho_mix", 0.2f} }, { 2, 3, 4, 7, 12 });
        createPreset("Skid Row - Skid Row", { {"ng_thresh", -25.f}, {"drive", 85.f}, {"dist_type", 0.f}, {"eq_low", 5.f}, {"eq_mid", 4.f}, {"eq_high", 7.f} }, { 0, 3, 4, 7 });
        createPreset("Skid Row - Slave to the Grind", { {"ng_thresh", -20.f}, {"drive", 90.f}, {"dist_type", 2.f}, {"eq_low", 7.f}, {"eq_mid", -2.f}, {"eq_high", 6.f} }, { 0, 3, 4, 7 });
        createPreset("Dimebag Darrell", { {"ng_thresh", -25.f}, {"ng_rel", 10.f}, {"drive", 100.f}, {"dist_type", 2.f}, {"eq_low", 9.f}, {"eq_mid", -15.f}, {"eq_high", 10.f} }, { 0, 3, 4, 7 });
        createPreset("Scorpions - Rock You Like A Hurricane", { {"drive", 75.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", 6.f}, {"eq_high", 6.f} }, { 3, 4, 7 });
        createPreset("AC/DC", { {"drive", 55.f}, {"dist_type", 0.f}, {"eq_low", 3.f}, {"eq_mid", 6.f}, {"eq_high", 4.f} }, { 3, 4, 7 });
        createPreset("Aerosmith", { {"drive", 60.f}, {"dist_type", 1.f}, {"eq_low", 4.f}, {"eq_mid", 5.f}, {"eq_high", 5.f} }, { 3, 4, 7 });
        createPreset("Bon Jovi", { {"drive", 80.f}, {"dist_type", 0.f}, {"cho_rate", 2.f}, {"cho_mix", 0.4f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f} }, { 3, 7, 12, 13 });
        createPreset("Led Zeppelin", { {"drive", 65.f}, {"dist_type", 0.f}, {"eq_low", 2.f}, {"eq_mid", 6.f}, {"eq_high", 5.f}, {"delay_time", 0.15f}, {"delay_mix", 0.1f} }, { 3, 4, 7, 13 });
        createPreset("Lynyrd Skynyrd - Free Bird", { {"drive", 60.f}, {"dist_type", 0.f}, {"eq_mid", 7.f}, {"eq_high", 4.f} }, { 3, 4, 7 });
        createPreset("Jimi Hendrix", { {"drive", 65.f}, {"dist_type", 2.f}, {"wah_rate", 0.f}, {"wah_depth", 0.5f}, {"eq_low", 3.f}, {"eq_high", 5.f} }, { 3, 4, 7, 8 });

        // --- SHREDDERS & VIRTUOSOS ---
        createPreset("Jason Becker - Perpetual Burn", { {"ng_thresh", -30.f}, {"drive", 85.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 5.f}, {"eq_high", 6.f}, {"delay_time", 0.35f}, {"delay_mix", 0.15f} }, { 0, 3, 4, 7, 13 });
        //createPreset("Jason Becker - Perspective", { {"cmp_thresh", -25.f}, {"cho_rate", 1.5f}, {"cho_mix", 0.6f}, {"delay_time", 0.4f}, {"delay_mix", 0.3f}, {"rvb_room", 0.6f}, {"rvb_mix", 0.3f} }, { 1, 7, 12, 13, 14 });
        createPreset("Buckethead - Live (Clean)", { {"cmp_thresh", -20.f},{"bst_gain", 20.0f}, {"phs_rate", 1.f}, {"phs_mix", 0.4f}, {"delay_time", 0.4f}, {"delay_mix", 0.2f}, {"rvb_room", 0.5f}, {"rvb_mix", 0.2f}}, {1, 7, 9, 13, 14});
        createPreset("Buckethead - Live (Distorted)", { {"ng_thresh", -35.f}, {"drive", 95.f}, {"dist_type", 2.f}, {"eq_low", 6.f}, {"eq_mid", -4.f}, {"eq_high", 7.f} }, { 0, 3, 4, 7 });
        createPreset("Buckethead - Soothsayer", { {"bst_gain", 20.f}, {"drive", 100.f}, {"dist_type", 0.f}, {"wah_rate", 3.f}, {"wah_depth", 0.7f}, {"delay_time", 0.45f}, {"delay_mix", 0.25f}}, {3, 7, 8, 13});
        createPreset("Guthrie Govan - Erotic Cakes", { {"drive", 65.f}, {"dist_type", 1.f}, {"eq_low", 3.f}, {"eq_mid", 6.f}, {"eq_high", -2.f}, {"delay_time", 0.4f}, {"delay_mix", 0.15f} }, { 3, 4, 7, 13 });
        createPreset("Paul Gilbert - Technical Difficulties", { {"ng_thresh", -35.f}, {"drive", 85.f}, {"dist_type", 0.f}, {"eq_low", 4.f}, {"eq_mid", 5.f}, {"eq_high", 6.f}, {"flg_rate", 0.5f}, {"flg_mix", 0.2f} }, { 0, 3, 4, 7, 10 });
        createPreset("John Petrucci", { {"ng_thresh", -35.f}, {"drive", 85.f}, {"dist_type", 0.f}, {"eq_low", 6.f}, {"eq_mid", -2.f}, {"eq_high", 5.f}, {"delay_time", 0.5f}, {"delay_feed", 0.3f}, {"delay_mix", 0.2f}, {"rvb_room", 0.4f}, {"rvb_mix", 0.1f} }, { 0, 3, 4, 7, 13, 14 });
        createPreset("Eric Johnson - Cliffs of Dover", { {"drive", 70.f}, {"dist_type", 2.f}, {"eq_low", 2.f}, {"eq_mid", 7.f}, {"eq_high", -6.f}, {"delay_time", 0.35f}, {"delay_feed", 0.4f}, {"delay_mix", 0.25f} }, { 3, 4, 7, 13 });

        // --- STUDIO & SYNTH EFFECTS ---
        //createPreset("Tone - Pipe Organ", { {"syn_type", 1.f}, {"syn_mix", 0.7f}, {"oct_semi", -12.f}, {"oct_mix", 0.6f}, {"cho_rate", 2.f}, {"cho_depth", 0.8f}, {"cho_mix", 0.5f}, {"rvb_room", 1.f}, {"rvb_damp", 0.2f}, {"rvb_mix", 0.7f} }, { 6, 12, 14, 16 });
        createPreset("Tone - Clean + Rev + Delay", { {"bst_gain", 24.f}, {"cmp_thresh", -20.f}, {"delay_time", 0.4f}, {"delay_mix", 0.3f}, {"rvb_room", 0.6f}, {"rvb_mix", 0.3f}}, { 2, 1, 7, 13, 14});
        createPreset("Tone - Clean + Rev", { {"bst_gain", 24.f}, {"cmp_thresh", -25.f}, {"rvb_room", 0.7f}, {"rvb_mix", 0.4f} }, { 2, 1, 7, 14 });
        createPreset("Tone - Glitter Clean", { {"bst_gain", 24.f}, {"cmp_thresh", -30.f}, {"cho_rate", 2.5f}, {"cho_depth", 0.7f}, {"cho_mix", 0.25f}, {"delay_time", 0.35f}, {"delay_mix", 0.4f} }, { 2, 1, 7, 12, 13 });
        //createPreset("Tone - Sitar Simulator", { {"ac_body", -5.f}, {"ac_air", 8.f}, {"ac_reso", 0.7f}, {"phs_rate", 6.f}, {"phs_depth", 0.4f}, {"rm_freq", 800.f}, {"rm_mix", 0.15f}, {"rvb_room", 0.4f}, {"rvb_mix", 0.2f} }, { 9, 14, 15, 19 });
        createPreset("Tone - Acoustic + Rev", { {"bst_gain", 20.f}, {"ac_body", 6.f}, {"ac_air", 7.f}, {"ac_reso", 0.6f}, {"rvb_room", 0.5f}, {"rvb_mix", 0.2f}}, { 2, 14, 15});
        createPreset("Tone - Harmonizer (+3rd)", { {"ps_semi", 4.f}, {"ps_mix", 0.5f}, {"drive", 60.f}, {"dist_type", 0.f} }, { 3, 5, 7 });
        createPreset("Tone - Lead Sub Octave", { {"oct_semi", -12.f}, {"oct_mix", 0.5f}, {"drive", 75.f}, {"dist_type", 2.f} }, { 3, 6, 7 });
        createPreset("Tone - Tremolo Surf", { {"bst_gain", 24.f}, {"trem_rate", 6.f}, {"trem_depth", 0.8f}, {"rvb_room", 0.4f}, {"rvb_mix", 0.5f} }, { 2, 7, 11, 14 });
        createPreset("Tone - Pad Guitar", { {"bst_gain", 20.f}, {"cmp_thresh", -40.f}, {"cho_rate", 1.f}, {"cho_mix", 0.8f}, {"delay_time", 0.8f}, {"delay_feed", 0.7f}, {"delay_mix", 0.6f}, {"rvb_room", 1.f}, {"rvb_mix", 0.8f} }, { 2, 1, 12, 13, 14 });
        createPreset("Tone - Lead + Reverse Delay", { {"drive", 60.f}, {"dist_type", 2.f}, {"delay_time", 0.6f}, {"delay_feed", 0.6f}, {"delay_mix", 0.7f}, {"rvb_room", 0.8f}, {"rvb_mix", 0.6f} }, { 3, 7, 13, 14 });
        createPreset("Tone - Grunge Muff", { {"drive", 85.f}, {"dist_type", 2.f}, {"eq_low", 6.f}, {"eq_mid", -4.f}, {"eq_high", 3.f}, {"cho_rate", 1.5f}, {"cho_mix", 0.2f} }, { 3, 4, 7, 12 });
        createPreset("Tone - Modern DJent", { {"ng_thresh", -25.f}, {"ng_att", 1.f}, {"ng_rel", 10.f}, {"bst_gain", 12.f}, {"drive", 95.f}, {"dist_type", 1.f}, {"eq_low", 8.f}, {"eq_mid", 4.f}, {"eq_high", 7.f} }, { 0, 2, 3, 4, 7 });
        createPreset("Tone - High Gain Lead", { {"ng_thresh", -30.f}, {"drive", 85.f}, {"dist_type", 0.f}, {"delay_time", 0.45f}, {"delay_feed", 0.3f}, {"delay_mix", 0.25f} }, { 0, 3, 4, 7, 13 });
        createPreset("Tone - Bass Synth", { {"oct_semi", -12.f}, {"oct_mix", 1.f}, {"cmp_thresh", -20.f} }, { 1, 6 });
    }
};