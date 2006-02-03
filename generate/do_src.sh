#!/bin/sh

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_PYTHON:=$PWD/../../telepathy-python}
else
  TP=${TELEPATHY_PYTHON:=$PWD/../telepathy-python}
fi

export PYTHONPATH=$TP:$PYTHONPATH

test -d generate && cd generate
cd src

echo Generating GabbleConnectionManager files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-connection-manager.xml GabbleConnectionManager

echo Generating GabbleConnection files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-connection.xml GabbleConnection

echo Generating GabbleIMChannel files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-im-channel.xml GabbleIMChannel

echo Generating GabbleMediaChannel files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-media-channel.xml GabbleMediaChannel

echo Generating GabbleMediaSessionHandler files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-media-session-handler.xml GabbleMediaSessionHandler

echo Generating GabbleMediaStreamHandler files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-media-stream-handler.xml GabbleMediaStreamHandler

echo Generating GabbleRosterChannel files ...
python $TP/tools/gengobject.py ../xml-modified/gabble-roster-channel.xml GabbleRosterChannel

echo Generating error enums ...
python $TP/tools/generrors.py
