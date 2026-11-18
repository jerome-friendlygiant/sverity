// sverity-args: --top top
// A 2-bit index reaches o[3:0] only: o[7:4] is never written, so the read of o sees an undefined value there (Established).
// expect: LNT-044 WARNING 8
module top(input logic [3:0] a, input logic [1:0] ix, output logic [7:0] y);
  function automatic logic [7:0] p(logic [3:0] s, logic [1:0] i);
    logic [7:0] o;
    o[i] = s[0];
    return o;
  endfunction
  assign y = p(a, ix);
endmodule
