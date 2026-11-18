// sverity-args: --top top
// The automatic function local is written only when k is set; otherwise the result is built from an undefined value.
// expect: LNT-044 WARNING 8
module top(input logic [3:0] a, input logic c, output logic [3:0] y);
  function automatic logic [3:0] f(input logic [3:0] v, input logic k);
    logic [3:0] s;
    if (k) s = v;
    return s;
  endfunction
  assign y = f(a, c);
endmodule
