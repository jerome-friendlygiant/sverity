// sverity-args: --top top
// Flow-insensitive Heuristic, no separate Unresolved outcome.
// expect: LNT-066 WARNING
module child #(parameter bit POISON=1)(input logic a, output logic y);
 logic gate; assign gate=POISON ? 1'bx : 1'b0;
 always_comb if(gate) y=a; else y=1'b0;
endmodule
module top(input logic a, output logic [2:0] y);
 child u(a,y[0]); child v(a,y[1]); child #(.POISON(0)) w(a,y[2]);
endmodule
