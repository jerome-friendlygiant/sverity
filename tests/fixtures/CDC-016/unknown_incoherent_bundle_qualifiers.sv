// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Distinct synchronized qualifiers across slices of a multi-bit bundle must not be certified as coherently qualified.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-coverage: payload-qualification INCOMPLETE
// expect-none: CDC-016
// expect-none: CDC-028
module top(input logic clka, clkb, rst_n, load1, load2, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  logic v_a, v1, v2, w_a, w1, w2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; w_a <= 1'b0; end
    else begin data <= d; v_a <= load1; w_a <= load2; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin {v2, v1} <= '0; {w2, w1} <= '0; end
    else begin {v2, v1} <= {v1, v_a}; {w2, w1} <= {w1, w_a}; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0;
    else begin
      if (v2) q[1:0] <= data[1:0];
      if (w2) q[3:2] <= data[3:2];
    end
endmodule
