// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An indirect next-state mux with a case branch over a synchronized qualifier stays Unresolved.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic clka, clkb, rst_n, load, din, output logic q);
  logic valid, v1, v2, data, next_q;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin valid <= 0; data <= 0; end else begin valid <= load; data <= din; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin v1 <= 0; v2 <= 0; end else begin v1 <= valid; v2 <= v1; end
  always_comb begin
    next_q = q;
    case (v2)
      1'b1: next_q = data;
      default: next_q = q;
    endcase
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= 0; else q <= next_q;
endmodule
