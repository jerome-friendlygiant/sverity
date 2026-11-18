// sverity-args: --top top
// Lookalike of good_return_value_assembled_by_elements: the low half is never written, so the return value is not established.
// expect: CST-010 WARNING
module top(input logic clk, input logic [3:0] d, output logic [7:0] q);
  function automatic logic [7:0] widen(input logic [3:0] v);
    widen[7:4] = v;
  endfunction
  always_ff @(posedge clk) q <= widen(d);
endmodule
