// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A correctly qualified payload always keeps its stability obligation, so the mutation is the payload crossing itself.
// expect: CDC-039 WARNING
// revert: else if (v2) q <= data; => else if (v2) q <= '1;
// revert-expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= '0; else if (v2) q <= data;
endmodule
