// sverity-args: --top top --sdc @DIR@/input_async.sdc
// expect-none: CDC-029 ERROR
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic valid, r1, r2;
  always_ff @(posedge src_clk) valid <= req;
  always_ff @(posedge dst_clk) begin
    r1 <= valid;
    r2 <= r1;
    if (r2) q <= data;
  end
endmodule
