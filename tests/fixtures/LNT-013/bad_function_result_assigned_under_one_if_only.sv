// sverity-args: --top top
// revert: if (g) expand = payload ^ 8'h80; => expand = payload ^ 8'h80;
// revert-expect-none: LNT-013
// The result is assigned only when `g` holds; the finding names that `if` as the incomplete statement.
// expect: LNT-013 WARNING 7
module top(input logic gate, input logic [7:0] data, output logic [7:0] value);
  function automatic logic [7:0] expand(input logic g, input logic [7:0] payload);
    if (g) expand = payload ^ 8'h80;
  endfunction
  assign value = expand(gate, data);
endmodule
