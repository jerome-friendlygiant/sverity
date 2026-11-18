// sverity-args: --top top
// slang's driver list stops at a decided `continue` and drops the write after the loop; the write still runs, and the second
// process drives the same variable.
// expect: LNT-009 ERROR 8
module top(input logic c, output logic y);
  logic g;
  always @(c) begin for (int i = 0; i < 4; i++) begin if (i == 1) continue; end g = c; end
  always @(c) g = 1'b0;
  assign y = g;
endmodule
