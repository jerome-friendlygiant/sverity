// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Defaulting data_d to raw cross-domain data in always_comb bypasses the qualifier when !v2.
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// expect-none: CDC-039
// revert: data_d = data; => data_d = q;
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    data_d = data;
    if (v2) data_d = data;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
