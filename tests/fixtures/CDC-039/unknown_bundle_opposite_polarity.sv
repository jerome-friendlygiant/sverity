// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Opposite qualifier polarities across bits of a multi-bit bundle must not be certified as coherently qualified.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-coverage: payload-qualification INCOMPLETE
// expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0;
    else begin
      if (v2) q[1:0] <= data[1:0];
      if (!v2) q[3:2] <= data[3:2];
    end
endmodule
