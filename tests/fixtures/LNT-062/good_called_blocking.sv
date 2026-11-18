// expect-none: LNT-062
// expect-none: LNT-063
module top(input logic [3:0] d, output logic [3:0] q);
  function automatic void fill();
    for (int i = 0; i < 4; i++) q[i] = d[i];
  endfunction
  always_comb fill();
endmodule
