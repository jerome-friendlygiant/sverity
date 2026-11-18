// sverity-args: --top top
// The break ends the loop inside the called function, not the caller's loop: i still reaches 8.
// expect: LNT-006 ERROR 10
module top(input logic c, input logic [7:0] x, output logic y);
  logic a [0:7];
  function automatic int ffs(input logic [7:0] v);
    ffs = 0;
    for (int k = 0; k < 8; k++) if (v[k]) begin ffs = k; break; end
  endfunction
  always @(c) for (int i = 0; i < 16; i++) a[i] = c ^ ffs(x)[0];
  assign y = a[0];
endmodule
