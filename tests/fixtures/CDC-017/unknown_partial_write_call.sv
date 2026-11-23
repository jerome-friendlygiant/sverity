// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect: CDC-017 WARNING
// expect-unresolved: CDC-017
// False Gray: partial write through an opaque user function.
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  function automatic logic f(input logic [3:0] v); f = v[3] ^ v[0]; endfunction
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0; else begin gray <= bin ^ (bin >> 1); gray[1] <= f(bin); end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
