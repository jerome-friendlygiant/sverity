// sverity-args: --top top --option CDC-005.min_stages=3 --sdc @DIR@/../CDC-029/input_async.sdc
// expect: CDC-005 ERROR
// expect-none: CDC-029 ERROR
// expect-none: CDC-007
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic first_stage;
  always_ff @(posedge dst_clk) begin
    first_stage <= req;
    q <= first_stage;
  end
endmodule
