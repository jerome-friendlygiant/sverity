// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_read_bus.json
// Independent per-bit chains do not establish a coherent bus transfer.
// expect: CDC-035 WARNING
module DPRAM(input logic WCLK, RCLK, input logic [1:0] WD, output logic [1:0] RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, output logic [1:0] q);
  logic [1:0] rd, s1, s2;
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WD(2'b00), .RD(rd));
  always_ff @(posedge clkb) begin s1 <= rd; s2 <= s1; end
  assign q = s2;
endmodule
