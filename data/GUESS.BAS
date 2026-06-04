10 REM GUESS.BAS - guess a number between 1 and 100
20 RANDOMIZE
30 LET N = RND(100)
40 LET T = 0
50 PRINT "I'm thinking of a number from 1 to 100."
60 INPUT "Your guess"; G
70 LET T = T + 1
80 IF G = N THEN GOTO 200
90 IF G < N THEN PRINT "Too low."
100 IF G > N THEN PRINT "Too high."
110 IF T < 10 THEN GOTO 60
120 PRINT "Out of guesses. It was"; N
130 END
200 PRINT "Got it in"; T; "tries!"
210 END
