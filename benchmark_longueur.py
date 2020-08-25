# ============================ IMPORTS ====================================
from os import path, getcwd
from sys import argv
import subprocess
import time



# ================== DEFINITION OF THE PATHS ==============================
biorseoDir = path.realpath(".")
jar3dexec = "/opt/jar3d_2014-12-11.jar"
bypdir = "/opt/BayesPairing/bayespairing/src"
byp2dir = "/opt/rnabayespairing2.git/bayespairing/src"
moipdir = "/opt/RNAMoIP/Src/RNAMoIP.py"
biokopdir = "/opt/biokop"





seq = "UUUGUUGGAGAGUUUGAUCCUGGCUCAGGGUGAACGCUGGCGGCGUGCCUAAGACAUGCAAGUCGUGCGGGCCGCGGGGUUUUACUCCGUGGUCAGCGGCGGACGGGUGAGUAACGCGUGGGUGACCUACCCGGAAGAGGGGGACAACCCGGGGAAACUCGGGCUAAUCCCCCAUGUGGACCCGCCCCUUGGGGUGUGUCCAAAGGGCUUUGCCCGCUUCCGGAUGGGCCCGCGUCCCAUCAGCUAGUUGGUGGGGUAAUGGCCCACCAAGGCGACGACGGGUAGCCGGUCUGAGAGGAUGGCCGGCCACAGGGGCACUGAGACACGGGCCCCACUCCUACGGGAGGCAGCAGUUAGGAAUCUUCCGCAAUGGGCGCAAGCCUGACGGAGCGACGCCGCUUGGAGGAAGAAGCCCUUCGGGGUGUAAACUCCUGAACCCGGGACGAAACCCCCGACGAGGGGACUGACGGUACCGGGGUAAUAGCGCCGGCCAACUCCGUGCCAGCAGCCGCGGUAAUACGGAGGGCGCGAGCGUUACCCGGAUUCACUGGGCGUAAAGGGCGUGUAGGCGGCCUGGGGCGUCCCAUGUGAAAGACCACGGCUCAACCGUGGGGGAGCGUGGGAUACGCUCAGGCUAGACGGUGGGAGAGGGUGGUGGAAUUCCCGGAGUAGCGGUGAAAUGCGCAGAUACCGGGAGGAACGCCGAUGGCGAAGGCAGCCACCUGGUCCACCCGUGACGCUGAGGCGCGAAAGCGUGGGGAGCAAACCGGAUUAGAUACCCGGGUAGUCCACGCCCUAAACGAUGCGCGCUAGGUCUCUGGGUCUCCUGGGGGCCGAAGCUAACGCGUUAAGCGCGCCGCCUGGGGAGUACGGCCGCAAGGCUGAAACUCAAAGGAAUUGACGGGGGCCCGCACAAGCGGUGGAGCAUGUGGUUUAAUUCGAAGCAACGCGAAGAACCUUACCAGGCCUUGACAUGCUAGGGAACCCGGGUGAAAGCCUGGGGUGCCCCGCGAGGGGAGCCCUAGCACAGGUGCUGCAUGGCCGUCGUCAGCUCGUGCCGUGAGGUGUUGGGUUAAGUCCCGCAACGAGCGCAACCCCCGCCGUUAGUUGCCAGCGGUUCGGCCGGGCACUCUAACGGGACUGCCCGCGAAAGCGGGAGGAAGGAGGGGACGACGUCUGGUCAGCAUGGCCCUUACGGCCUGGGCGACACACGUGCUACAAUGCCCACUACAAAGCGAUGCCACCCGGCAACGGGGAGCUAAUCGCAAAAAGGUGGGCCCAGUUCGGAUUGGGGUCUGCAACCCGACCCCAUGAAGCCGGAAUCGCUAGUAAUCGCGGAUCAGCCAUGCCGCGGUGAAUACGUUCCCGGGCCUUGUACACACCGCCCGUCACGCCAUGGGAGCGGGCUCUACCCGAAGUCGCCGGGAGCCUACGGGCAGGCGCCGAGGGUAGGGCCCGUGACUGGGGCGAAGUCGUAACAAGGUAGCUGUACCGGAAGGUGCGGCUGGAUCACCUCCUUUCU"

step = 100
n = len(seq)

while step < len(seq)+50:
	sub_seq = seq[0:(min(step,n))]

	fasta = open("ZDFS33.fa", 'w')
	fasta.write(">__'ZDFS33 : 0-" + str(len(sub_seq)) + "'\n" + sub_seq)
	fasta.close()

	cmd = ["./bin/biorseo", "-d", "./data/modules/DESC", "-s", "./ZDFS33.fa", "-v"]

	old_time = time.time()
	output = subprocess.check_output(cmd)
	run_time = time.time() - old_time

	print("aaaah " + str(len(output)))

	"""
	if "Quitting because combinatorial issues" in output :
		#blaaaa

	else:
	"""








	step += 50