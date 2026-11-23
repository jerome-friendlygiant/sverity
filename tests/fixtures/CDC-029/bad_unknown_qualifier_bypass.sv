// sverity-args: --top top --sdc @DIR@/input_async.sdc
// expect: CDC-029 ERROR
// expect-none: CDC-039
module top(input logic src_clk, dst_clk, req, data, en, local_enable, output logic q);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= en;
    r2 <= r1;
    if (r2 || local_enable) q <= data;
  end
endmodule
