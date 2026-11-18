// sverity-args: --top top
// An index that calls a user subroutine with static state is not a constant: slang would fold the call by running it once, so
// the index may reach the X bit. Both shapes: a default-lifetime function's local, and a static local of an automatic function.
// expect: LNT-039 WARNING 10
// expect: LNT-039 WARNING 11
module top(input logic c, output logic y, z);
  localparam logic [3:0] P = 4'bxxx1;
  function logic [1:0] nxt(); int k; k = k + 1; return 2'(k - 1); endfunction
  function automatic logic [1:0] g(); static int s = 0; s++; return 2'(s); endfunction
  always_ff @(posedge c) y <= P[nxt()];
  always_ff @(posedge c) z <= P[g()];
endmodule
