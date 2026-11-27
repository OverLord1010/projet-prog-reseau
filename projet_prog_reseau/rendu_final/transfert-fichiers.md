# Transfert de fichiers — jalon 3

## Convention de fin de transfert

Sur la connexion TCP directe, l'expéditeur envoie des trames `FILE_SEND` contenant jusqu'à 4 096 octets de données chacune. Une trame `FILE_SEND` avec `payload_len = 0` marque la fin du fichier. Elle suit le dernier bloc même si celui-ci fait exactement 4 096 octets. Pour un fichier vide, elle constitue l'unique trame `FILE_SEND`.

Le destinataire envoie un `FILE_ACK` sans payload après avoir sauvegardé le fichier sous son nom définitif dans `.resa1/inbox/`. Le champ `infos` contient le nom du fichier dans les trames `FILE_SEND` et `FILE_ACK`. L'expéditeur considère la sauvegarde comme confirmée à la réception de cet accusé.

## Essais de transfert avec deux clients

Depuis la racine du dépôt, compilez le serveur et le client, puis préparez deux répertoires de test. Chaque client enregistre ses fichiers reçus dans son propre répertoire `.resa1/inbox/`.

```sh
essai=$(mktemp -d /tmp/resa1.XXXXXX)
cc travail/server.c -o "$essai/server"
cc travail/client.c -o "$essai/client"
mkdir "$essai/Alice" "$essai/Bob"
: > "$essai/Alice/vide.bin"
dd if=/dev/urandom of="$essai/Alice/bloc.bin" bs=4096 count=1
dd if=/dev/urandom of="$essai/Alice/multi.bin" bs=1000 count=10
dd if=/dev/urandom of="$essai/Bob/retour.bin" bs=1000 count=5
printf 'Répertoire de test : %s\n' "$essai"
"$essai/server" 54321
```

Ouvrez deux autres terminaux. Dans chacun, définissez `essai` avec le chemin affiché (remplacez `XXXXXX` par le suffixe affiché), puis lancez le client depuis le répertoire indiqué.

```sh
# Terminal Alice
essai=/tmp/resa1.XXXXXX
cd "$essai/Alice" && "$essai/client" 127.0.0.1 54321
```

```sh
# Terminal Bob
essai=/tmp/resa1.XXXXXX
cd "$essai/Bob" && "$essai/client" 127.0.0.1 54321
```

Saisissez `/nick Alice` et `/nick Bob` dans leurs clients respectifs. Dans un quatrième terminal, définissez aussi `essai` avec le chemin affiché pour exécuter les vérifications.

| Expéditeur et commande | Réponse du destinataire | Vérification |
| --- | --- | --- |
| Alice · `/send Bob vide.bin` | Bob · `N` | Alice voit le refus. `test ! -e "$essai/Bob/.resa1/inbox/vide.bin"` réussit. |
| Alice · `/send Bob vide.bin` | Bob · `Y` | `cmp "$essai/Alice/vide.bin" "$essai/Bob/.resa1/inbox/vide.bin"` |
| Alice · `/send Bob bloc.bin` | Bob · `Y` | `cmp "$essai/Alice/bloc.bin" "$essai/Bob/.resa1/inbox/bloc.bin"` |
| Alice · `/send Bob multi.bin` | Bob · `Y` | `cmp "$essai/Alice/multi.bin" "$essai/Bob/.resa1/inbox/multi.bin"` |
| Bob · `/send Alice retour.bin` | Alice · `Y` | `cmp "$essai/Bob/retour.bin" "$essai/Alice/.resa1/inbox/retour.bin"` |

Pour chaque ligne, attendez l'invite `Accept ? [Y/N]` avant de répondre. Après un `Y`, attendez la confirmation de sauvegarde chez l'expéditeur avant de lancer `cmp`. Si les deux fichiers sont identiques, `cmp` est silencieux et renvoie 0.

## Résultats du rejeu

Le refus avec `N` a été signalé à Alice et aucune copie n'a été créée.
Chaque acceptation avec `Y` a produit une confirmation de sauvegarde chez
l'expéditeur. Les quatre commandes `cmp` ont réussi pour le fichier vide,
le bloc de 4 096 octets, le binaire de 10 000 octets et l'envoi de Bob vers
Alice de 5 000 octets. Le message de confirmation affiche « Bob » dans les
deux sens.
