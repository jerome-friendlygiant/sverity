// sverity-args: --top top
// The break sits in a case arm whose item repeats an earlier arm's: the first arm that matches is taken, so the break never runs
// and every iteration writes v[j], including v[7].
// expect: LNT-009 ERROR 8
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int j = 0; j < 8; j++) begin case (j) 3: ; 3: break; default: ; endcase v[j] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
