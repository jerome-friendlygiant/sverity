// sverity-args: --top top --sdc @DIR@/input_async.sdc
// The payload and request launch from src; en has no launch contract, so its
// destination synchronizer cannot establish same-source payload qualification.
// expect: CDC-029 WARNING
// expect-none: CDC-029 ERROR
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 unresolved
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic src_clk, dst_clk, req, data, en, output logic q);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= en;
    r2 <= r1;
    if (r2) q <= data;
  end
endmodule
