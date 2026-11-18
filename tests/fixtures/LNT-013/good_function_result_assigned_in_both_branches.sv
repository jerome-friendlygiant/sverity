// sverity-args: --top top
// Lookalike of bad_function_result_assigned_under_one_if_only: the else assigns the result too.
// expect-none: LNT-013
module top(input logic gate, input logic [7:0] data, output logic [7:0] value);
  function automatic logic [7:0] expand(input logic g, input logic [7:0] payload);
    if (g) expand = payload ^ 8'h80;
    else expand = payload;
  endfunction
  assign value = expand(gate, data);
endmodule
