// sverity-args: --top top --goal cdc --sdc @DIR@/../_common/async.sdc
// expect-unresolved: CDC-039
// expect-none: CDC-039 INFO
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin : blk
    logic take, sampled;
    data_d = q;
    sampled = take;
    take = v2;
    if (sampled) data_d = data;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
