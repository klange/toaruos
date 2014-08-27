#
# klange's ~/.bashrc
#

[ -z "$PS1" ] && return

# DEFAULTS
KLANGE_USE_GIT=false
KLANGE_USE_SVN=false
KLANGE_USE_HG=false

if [[ "$(uname)" == "Darwin" || "$(uname)" == *CYGWIN* ]] ; then
	HOSTNAME=`hostname`
	alias ls="ls -G"
	export PATH="/Applications/Xcode.app/Contents/Developer/usr/bin:$PATH"
else
	HOSTNAME=`hostname --long`
fi

# ~/bin
if [ -e ~/bin ]; then
	export PATH=~/bin:$PATH
fi

# SPECIAL OPTIONS AND FIXES

export HISTCONTROL=$HISTCONTROL${HISTCONTROL+,}ignoredups
export HISTCONTROL=ignoreboth
shopt -s histappend
shopt -s checkwinsize
[ -x /usr/bin/lesspipe ] && eval "$(SHELL=/bin/sh lesspipe)"
if [ -z "$debian_chroot" ] && [ -r /etc/debian_chroot ]; then
	debian_chroot=$(cat /etc/debian_chroot)
fi

# Fix gnome-terminal color support
if [ "$COLORTERM" == "gnome-terminal" ]; then
	export TERM="xterm-256color"
elif [ "$COLORTERM" == "mate-terminal" ]; then
	export TERM="xterm-256color"
elif [ "$COLORTERM" == "Terminal" ]; then
	# XFCE Terminal
	export TERM="xterm-256color"
elif [ "$COLORTERM" == "xfce4-terminal" ]; then
	export TERM="xterm-256color"
elif [ "$FBTERM" == "1" ]; then
	export TERM="fbterm"
elif [ "$TERM" == "xterm" ]; then
	# If shell reports just 'xterm', it may be PuTTY
	if [ -e ~/bin/answerback ]; then
		export ANSWERBACK=$(~/bin/answerback)
		if [ "x$ANSWERBACK" == "xPuTTY" ]; then
			export TERM="xterm-256color"
			export COLORTERM="putty-256color"
			export LANG="C"
		fi
	fi
fi

if [ "$TERMINAL_OVERRIDE" != "" ]; then
	# Some things just refuse to accept their own
	# configuration options for these things.
	export TERM=$TERMINAL_OVERRIDE
fi

# Tango palette for framebuffers
function color_palette () {
	echo -en "\e]P02e3436" #black
	echo -en "\e]P8555753" #darkgray
	echo -en "\e]P1cc0000" #darkred
	echo -en "\e]P9ef2929" #red
	echo -en "\e]P24e9a06" #darkgreen
	echo -en "\e]PA8ae234" #green
	echo -en "\e]P3c4a000" #brown
	echo -en "\e]PBfce94f" #yellow
	echo -en "\e]P43465a4" #darkblue
	echo -en "\e]PC729fcf" #blue
	echo -en "\e]P575507b" #darkmagenta
	echo -en "\e]PDad7fa8" #magenta
	echo -en "\e]P606989a" #darkcyan
	echo -en "\e]PE34e2e2" #cyan
	echo -en "\e]P7ffffff" #lightgray
	echo -en "\e]PFeeeeec" #white
}
if [ "$TERM" == "linux" ]; then
	color_palette
fi

if [ "$TERM" == "screen-bce" ]; then
	# I use screen under 256-color-supportive things
	# far more often than not, so give me 256-colors
	export TERM=screen-256color
fi

# PROMPT
function prompt_command {
	local RETURN_CODE="$?"

	local COLOR_P="\033[38;5;"
	local COLOR_A="m"
	if [ "$TERM" == "fbterm" ] ; then
		COLOR_P="\033[1;"
		COLOR_A="}"
	fi
	local   SOFT_YELLOW="\[${COLOR_P}221$COLOR_A\]"
	local   MEDIUM_GRAY="\[${COLOR_P}59$COLOR_A\]"
	local     SOFT_BLUE="\[${COLOR_P}81$COLOR_A\]"
	local    LIGHT_GRAY="\[${COLOR_P}188$COLOR_A\]"
	local    LIGHT_GOLD="\[${COLOR_P}222$COLOR_A\]"
	local MEDIUM_ORANGE="\[${COLOR_P}173$COLOR_A\]"
	local    MEDIUM_RED="\[${COLOR_P}167$COLOR_A\]"
	local  MEDIUM_GREEN="\[${COLOR_P}47$COLOR_A\]"
	local    BRIGHT_RED="\[${COLOR_P}196$COLOR_A\]"
	local  LIGHT_PURPLE="\[${COLOR_P}177$COLOR_A\]"
	local         RESET="\[\033[0m\]"
	local          BOLD="\[\033[1m\]"
	local          SAVE="\[\033[s\]"
	local       RESTORE="\[\033[u\]"

	if [ "$TERM" == "linux" ] ; then
		  SOFT_YELLOW="\[\033[1;33m\]"
		  MEDIUM_GRAY="\[\033[1;30m\]"
			SOFT_BLUE="\[\033[1;34m\]"
		   LIGHT_GRAY="\[\033[1;30m\]"
		   LIGHT_GOLD="\[\033[1;33m\]"
		MEDIUM_ORANGE="\[\033[1;33m\]"
		   MEDIUM_RED="\[\033[1;31m\]"
		 MEDIUM_GREEN="\[\033[1;32m\]"
		   BRIGHT_RED="\[\033[1;31m\]"
		 LIGHT_PURPLE="\[\033[1;35m\]"
	fi

	local ALIGN_LEFT="\033[1G"
	local ALIGN_RIGHT="\033[400C"
	local MAKE_SPACE="\033[16D"

	local DATE_STRING="\D{%m/%d}"
	local TIME_STRING="\t"

	local CURRENT_PATH="\w"
	if [ -e ~/bin/shorten_pwd ] ; then
		CURRENT_PATH=`~/bin/shorten_pwd`
	fi

	local TITLEBAR=""
	case $TERM in
		xterm*|*rxvt*|cygwin|interix|Eterm|mlterm|kterm|aterm|putty*)
			if [ "${STY}" ] ; then
				 TITLEBAR="\[\ek\u@\h:$CURRENT_PATH\e\134\]"
			else
				TITLEBAR="\[\e]1;\u@\h:$CURRENT_PATH\007\e]2;\u@\h:$CURRENT_PATH\007\]"
			fi
		;;
		toaru*)
			TITLEBAR="\[\e]1;\u@\h:$CURRENT_PATH\007\]"
		;;
		screen*)
			TITLEBAR="\[\ek\u@\h:$CURRENT_PATH\e\134\]"
		;;
	esac

	local PROMPT_COLOR="$MEDIUM_GREEN"
	if [[ ${EUID} == 0 ]] ; then
		PROMPT_COLOR="$BRIGHT_RED"
	fi

	local PROMPT="$BOLD"
	PROMPT="$PROMPT$SAVE\[$ALIGN_RIGHT$MAKE_SPACE\]" # Ram the cursor to the right, then back 16 spaces
	PROMPT="$PROMPT$MEDIUM_GRAY\[[\]$MEDIUM_ORANGE\[$DATE_STRING \]$MEDIUM_RED\[$TIME_STRING\]$MEDIUM_GRAY\[]\]"
	PROMPT="$PROMPT$RESTORE" # Reset the cursor to the left side
	PROMPT="$PROMPT$SOFT_YELLOW\u$MEDIUM_GRAY@$SOFT_BLUE\h "

	if [ $KLANGE_USE_GIT == true ]; then
		local GIT_STATUS="$(git status 2>/dev/null)"
		if [[ $GIT_STATUS != "" ]] ; then
			local REFS="$(git symbolic-ref HEAD 2>/dev/null | sed 's/.*\///')"
			REFS="$LIGHT_GOLD${REFS#refs/heads/}"
			if [[ `echo $GIT_STATUS | grep "modified:"` != "" ]] ; then
				REFS="$REFS$LIGHT_PURPLE*" # Modified
			elif [[ `echo $GIT_STATUS | grep "renamed:"` != "" ]] ; then
				REFS="$REFS$LIGHT_PURPLE*" # Modified as well
			fi
			if [[ `echo $GIT_STATUS | grep "ahead of"` != "" ]] ; then
				REFS="$REFS$SOFT_BLUE^" # Staged
			fi 
			PROMPT="$PROMPT$REFS "
		fi
	fi

	if [[ $RETURN_CODE != 0 ]] ; then
		PROMPT="$PROMPT$MEDIUM_RED$RETURN_CODE "
	fi

	PROMPT="$PROMPT$RESET$LIGHT_GRAY$CURRENT_PATH$BOLD$PROMPT_COLOR\\\$ $RESET"
	PS1="$TITLEBAR$PROMPT"
}

export PROMPT_COMMAND=prompt_command

# COLOR SUPPOORT

if [ -x /usr/bin/dircolors ]; then
	eval "`dircolors -b`"
	alias ls='ls --color=auto'
fi

# TAB COMPLETION

if [ -f /etc/bash_completion ]; then
	. /etc/bash_completion
fi

# Extra aliases
if [ -e ~/.bash_aliases ]; then
	. ~/.bash_aliases
fi

alias :qall="echo \"This isn't vim :P\" && exit"
