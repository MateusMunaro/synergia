#!/bin/bash

# Wrapper inteligente para myvc que lida com permissões automaticamente

MYVC_BIN="$(dirname "$0")/cmake-build-debug-wsl/untitled"

# Função para detectar WSL
is_wsl() {
    grep -qi microsoft /proc/version 2>/dev/null || grep -qi wsl /proc/version 2>/dev/null
}

# Função para verificar se estamos em mount do Windows
is_windows_mount() {
    [[ "$(pwd)" == /mnt/* ]]
}

# Função para sugerir alternativas
suggest_alternatives() {
    echo "❌ Problema de permissão detectado!"
    echo
    echo "Opções disponíveis:"
    echo
    echo "1. Usar diretório Linux nativo (Recomendado):"
    echo "   cd ~/myvc_projects"
    echo "   $0 $*"
    echo
    echo "2. Forçar execução com sudo (Não recomendado):"
    echo "   sudo $0 $*"
    echo
    echo "3. Criar projeto em local alternativo:"
    echo "   mkdir -p ~/myvc_projects/$(basename "$(pwd)")"
    echo "   cd ~/myvc_projects/$(basename "$(pwd)")"
    echo "   $0 init"
}

# Função para criar link simbólico se necessário
create_project_link() {
    local source_dir="$1"
    local link_name="$2"

    if [ ! -e "$link_name" ]; then
        ln -s "$source_dir" "$link_name" 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "✓ Link criado: $link_name -> $source_dir"
        fi
    fi
}

# Processar comando init especialmente
if [ "$1" = "init" ] && is_wsl && is_windows_mount; then
    echo "⚠️  Detectado: WSL em diretório Windows"
    echo

    # Tentar criar em local alternativo
    ALT_DIR="$HOME/myvc_projects/$(basename "$(pwd)")"
    echo "Criando projeto alternativo em: $ALT_DIR"

    mkdir -p "$ALT_DIR"
    cd "$ALT_DIR"

    # Executar init no diretório alternativo
    "$MYVC_BIN" "$@"
    RESULT=$?

    if [ $RESULT -eq 0 ]; then
        echo
        echo "✓ Projeto criado com sucesso em: $ALT_DIR"
        echo
        echo "Para acessar do Windows:"
        echo "  \\\\wsl$\\$(lsb_release -i -s 2>/dev/null || echo "Ubuntu")\\${ALT_DIR#/}"

        # Criar arquivo de referência no diretório original
        ORIGINAL_DIR="$OLDPWD"
        echo "$ALT_DIR" > "$ORIGINAL_DIR/.myvc_location" 2>/dev/null

        echo
        echo "Dica: cd $ALT_DIR && myvc watch"
    fi

    exit $RESULT
fi

# Para outros comandos, executar normalmente
"$MYVC_BIN" "$@"
RESULT=$?

# Se falhar com erro de permissão, oferecer ajuda
if [ $RESULT -ne 0 ]; then
    if [[ "$1" == "init" ]] || [[ "$1" == "watch" ]]; then
        # Verificar se é problema de permissão
        if ! touch .myvc_test_$$ 2>/dev/null; then
            suggest_alternatives "$@"
        else
            rm -f .myvc_test_$$
        fi
    fi
fi

exit $RESULT