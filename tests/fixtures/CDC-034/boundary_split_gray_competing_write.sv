// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Competing write to an overlapping bit prevents split-Gray reassembly and must still be flagged as CDC-009.
// expect: CDC-009 WARNING
// expect-none: CDC-034 INFO
module top(input logic clka, clkb, rst_n, inc_a, extra, output logic [3:0] qa);
  logic [3:0] ab, ag, as1, as2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin ab <= '0; ag <= '0; end
    else begin
      if (inc_a) ab <= ab + 1'b1;
      ag[3]   <= ab[3];
      ag[2:0] <= ab[2:0] ^ ab[3:1];
      if (extra) ag[1] <= ab[0];
    end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin as1 <= '0; as2 <= '0; end
    else begin as1 <= ag; as2 <= as1; end
  assign qa = as2;
endmodule
