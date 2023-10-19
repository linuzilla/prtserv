/***************************************************************************\
|* pclcount --- Conta as paginas de um arquivo de impressao formato HP PCL *|
|* Copyright (c) 2003, by Eduardo Gielamo Oliveira & Rodolfo Broco  Manin  *|
|*									   *|
|* Este programa e software livre; voce pode redistribui-lo e/ou 	   *|
|* modifica-lo sob os termos da Licenca Publica Geral GNU, conforme 	   *|
|* publicada pela Free Software Foundation; tanto a versao 2 da 	   *|
|* Licenca como (a seu criterio) qualquer versao mais nova. 		   *|
|*									   *|
|* Este programa e distribuido na expectativa de ser util, mas SEM 	   *|
|* QUALQUER GARANTIA; sem mesmo a garantia implicita de 		   *|
|* COMERCIALIZACAO ou de ADEQUACAO A QUALQUER PROPOSITO EM 		   *|
|* PARTICULAR. Consulte a Licenca Publica Geral GNU para obter mais 	   *|
|* detalhes. 								   *|
|*									   *|
|* Voce deve ter recebido uma copia da Licenca Publica Geral GNU 	   *|
|* junto com este programa; se nao, escreva para a Free Software 	   *|
|* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 		   *|
|* 02111-1307, USA. 							   *|
|*------------------------------------------------------------------------ *|
|* Este programa deve ser eficiente para contar o numero de paginas em 	   *|
|* arquivos PCL gerados pelos drivers padrao do Windows. 		   *|
|* Contamos com a colaboracao dos usuarios para aprimora-lo. 		   *|
|*									   *|
|* Caso voce encontre algum arquivo nessas condicoes que produza uma 	   *|
|* contagem erronea, por favor entre em contato conosco por e-mail, 	   *|
|* anexando o arquivo PCL que foi contado errado e informando a versao 	   *|
|* do PCLCount que foi usada e o numero de paginas real do arquivo. 	   *|
|*									   *|
|* Atualmente, nao suportamos arquivos PCL gerados por drivers fornecidos  *|
|* por terceiros. Apenas os drivers que acompanham o Windows sao 	   *|
|* suportados. 								   *|
|* 									   *|
|*									   *|
\***************************************************************************/
#define Version 20040107

#include <stdio.h>
#include <libgen.h>


// Mostra o help da linha de comandos
void ShowUsage() {
	printf("pclcount (v.%d): Conta o numero de paginas em um arquivo PCL.\n" \
		"Copyright (c) 2003, by Eduardo Gielamo Oliveira & Rodolfo Broco Manin\n" \
		"Este programa e' distribuido nos termos da Licenca Publica GPL.\n" \
		"Para maiores informacoes, consulte http://www.gnu.org/copyleft/gpl.html\n\n" \
		"Sintaxe: pclcount <nome_do_arquivo> [-v][-h]\n" \
		" -v -- Exibe informacoes detalhadas durante a execucao\n" \
		" -h -- Informa os parametros validos para linha de comandos\n", Version);
}

int main (int argc, char **argv) {
	FILE *InputFile;
	char ch, EndTag, tag[2], *InputFileName;
	int n, BlockSize, Pages, Copies, Quiet;
	unsigned long FileSize, FilePos;

	BlockSize = Pages = FileSize = FilePos = 0;
	Copies = Quiet = 1;
	InputFileName = NULL;

	// Intepreta os parametros da linha de comandos
	for(n = 1; n <= argc - 1; n++) {
		if(! memcmp(argv[n], "-h", 2)) {
			ShowUsage();
			exit(0);
		} else if(! memcmp(argv[n], "-v", 2))
				Quiet = 0;
			else if(argv[n][0] == '-') {
				fprintf(stderr, "-- Parametro incorreto: '%s'.\n",argv[n]);
				exit(1);
				} else
					InputFileName = argv[n];
				}

			if(InputFileName == NULL) {
				fprintf(stderr, "-- Nao foi informado um nome de arquivo.\n" \
					" Use 'pclcount -h' para obter ajuda.\n");
				exit(1);
			}

			// Tenta abrir o arquivo de entrada
			if(! (InputFile = fopen(InputFileName, "r"))) {
				fprintf(stderr, "--Erro abrindo arquivo: %s\n", argv[1]);
				exit(-1);
};

// Obtem o tamanho do arquivo, para exibir as estatisticas caso especificado '-v' na linha de comandos
if(! Quiet) {
	fseek(InputFile, 0, SEEK_END);
	FileSize = ftell(InputFile);
	fseek(InputFile, 0, SEEK_SET);
}

while(fread(&ch, 1, 1, InputFile)) {
	if(ch == 12){
		Pages++;
	}
	printf ("%ld\n",ch);
	printf("PAGINAS %ld\n",Pages);
	return(0);
	switch(ch) {
		case 12:
		// Encontrado FormFeed: incrementa o contador de paginas
			Pages ++;
			break;
		case 27:
		// Encontrado <ESC>
			fread(tag, 2, 1, InputFile);
			if(! (memcmp(tag, "*b", 2) && memcmp(tag, "(s", 2) && memcmp(tag, ")s", 2) && memcmp(tag, "&p", 2))) {
				/*Detecta os operadores:
				<ESC>*b###W -> Inicio de Bloco Binario
				<ESC>(s###W -> Inicio de Bloco de Descricao de Caracteres
				<ESC>)s###W -> Inicio de Bloco de Descricao de Fontes
				<ESC>&p###X -> Inicio de Bloco de Caracteres nao-imprimiveis
				Nesses operadores, '###' eh o tamanho do bloco respectivo.
				*/
				// Define o caracter terminador do bloco
				EndTag = memcmp(tag, "&p", 2) ? 'W' : 'X';
				do {
					fread(&ch, 1, 1, InputFile);
					if((ch >= '0') && (ch <= '9')) {
						// Foi lido um numero: compoe o tamanho do bloco
						BlockSize = 10 * BlockSize + ch - '0';
					}
				} while ((ch >= '0') && (ch <='9'));
				if(ch == EndTag) {
					// O operador terminou com 'W': eh um dos operadores esperados
					// Efetua um 'seek' para pular o bloco
					fseek(InputFile, BlockSize, SEEK_CUR);
					FilePos = ftell(InputFile);
					// Atualizando a mensagem de status aqui (inves de faze-lo o tempo todo) nao deixa o processamento tao lento
					if(! Quiet) printf("Processando... %ld de %ld bytes (%ld%%)\r", FilePos, FileSize, (FilePos * 100) / FileSize);
					}
					// Nao era um dos operadores esperados: reinicializa BlockSize
					BlockSize = 0;
				} else if(! (memcmp(tag, "&l", 2))) {
						// O operador <ESC>&l###X informa o numero de copias ('###') solicitadas
						n = 0;
						for(ch = '0'; (ch >= '0') && (ch <= '9'); 
						fread(&ch,1, 1, InputFile)) {
							n = 10 * n + ch - '0';
						}
						if(ch == 'X') {
							// O operador terminou com 'X' (como esperado). Obtem o numero de copias
							Copies = n;
						}
				}
			break;
	}
}

fclose(InputFile);

if(Quiet)
	// Caso nao tenha sido especificado '-v' na linha de comandos, imprime apenas o numero total de folhas do trabalho
	printf("%d\n", Pages * Copies);
else
	printf("Processando...Conluido. \n" \
		"Numero de Paginas.....: %d\n" \
		"Numero de Copias......: %d\n" \
		"Total de Paginas......: %d\n", Pages, Copies, Pages * Copies);
return(0);
}