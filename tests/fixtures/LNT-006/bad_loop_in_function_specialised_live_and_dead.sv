// sverity-args: --top top
// The module is instantiated twice: g is called in one specialisation and not in the other. The overrun is reported once, as the error.
// expect: LNT-006 ERROR 8
// expect-none: LNT-006 WARNING
module m #(parameter bit USE = 0)(input logic [3:0] a, output logic y);
  function automatic logic g(input logic [3:0] v);
    logic r = 0;
    for (int i = 0; i < 5; i++) r ^= v[i];
    return r;
  endfunction
  if (USE) begin : gg assign y = g(a); end else begin : gn assign y = ^a; end
endmodule
module top(input logic [3:0] a, output logic [1:0] y);
  m #(0) u0(.a(a), .y(y[0])); m #(1) u1(.a(a), .y(y[1]));
endmodule
