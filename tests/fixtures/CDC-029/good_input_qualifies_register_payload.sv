// sverity-args: --top top --sdc @DIR@/input_async.sdc
// expect-none: CDC-003 ERROR
// expect-none: CDC-028 ERROR
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic payload, r1, r2;
  always_ff @(posedge src_clk) payload <= data;
  always_ff @(posedge dst_clk) begin
    r1 <= req;
    r2 <= r1;
    if (r2) q <= payload;
  end
endmodule
