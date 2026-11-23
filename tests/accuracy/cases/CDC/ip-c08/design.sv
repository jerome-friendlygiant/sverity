// C08
module c08(input logic wr_clk, rd_clk, input logic [3:0] wr_bin, output logic [3:0] wr_seen);
  logic [3:0] p1;
  always_ff @(posedge rd_clk) begin p1<=wr_bin; wr_seen<=p1; end
endmodule
