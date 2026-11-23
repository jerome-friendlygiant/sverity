// A second source-data write under a local condition bypasses the synchronized qualifier.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, din, sel, output logic q);
  logic valid, v1, v2, data;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin valid <= 0; data <= 0; end else begin valid <= load; data <= din; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin v1 <= 0; v2 <= 0; end else begin v1 <= valid; v2 <= v1; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= 0; else if (v2) q <= data; else if (sel) q <= data;
endmodule
