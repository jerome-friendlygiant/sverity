// sverity-args: --top top --sdc @DIR@/data_input_async.sdc
// expect: CDC-029 WARNING
// expect-confidence: CDC-029 unresolved
// expect-none: CDC-029 ERROR
// expect-coverage: graph-precision INCOMPLETE
module top(input logic src_clk, dst_clk, data, output logic q);
  function automatic logic opaque(input logic x); return 1'b0; endfunction
  always_ff @(posedge dst_clk) q <= opaque(data);
endmodule
