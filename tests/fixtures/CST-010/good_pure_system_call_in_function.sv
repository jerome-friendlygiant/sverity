// sverity-args: --top top
// $clog2 is a function of its argument alone: calling it inside a function reads and writes nothing else.
// expect-none: CST-010
module top(input logic clk, input logic [7:0] d, output logic [3:0] q);
  function automatic logic [3:0] bits_of(input int v);
    return (v == 1) ? 4'd1 : 4'($clog2(v));
  endfunction
  always_ff @(posedge clk) q <= bits_of(int'(d));
endmodule
