#!/bin/bash
# Liste tous les fichiers RBT disponibles sur chaque CD

echo "🔍 Fichiers Robot (.RBT) disponibles"
echo "===================================="
echo

for cd_dir in /workspaces/extractor_sierra/ROBOT_CD*; do
    if [ -d "$cd_dir" ]; then
        cd_name=$(basename "$cd_dir")
        rbt_count=$(find "$cd_dir" -name "*.RBT" -o -name "*.rbt" 2>/dev/null | wc -l)
        
        if [ $rbt_count -gt 0 ]; then
            echo "📀 $cd_name: $rbt_count fichiers RBT"
            
            # Lister les numéros
            find "$cd_dir" -name "*.RBT" -o -name "*.rbt" 2>/dev/null | while read f; do
                basename "$f" .RBT | sed 's/\.rbt$//'
            done | sort -n | head -20
            
            if [ $rbt_count -gt 20 ]; then
                echo "   ... et $((rbt_count - 20)) autres"
            fi
            echo
        fi
    fi
done

echo "===================================="
echo "🎯 Recherche de fichiers spécifiques:"
echo

for num in 91 161 230 1000 1014 1180; do
    found=0
    for cd_dir in /workspaces/extractor_sierra/ROBOT_CD*; do
        if [ -f "$cd_dir/$num.RBT" ] || [ -f "$cd_dir/$num.rbt" ]; then
            cd_name=$(basename "$cd_dir")
            echo "   ✅ $num.RBT trouvé dans $cd_name"
            found=1
            break
        fi
    done
    
    if [ $found -eq 0 ]; then
        echo "   ❌ $num.RBT non trouvé"
    fi
done
