// Read-data mux between two banks with a registered copy of the last read.
// Interface:
//   rd_a, rd_b  read data from bank A and bank B
//   sel_b       selects bank B when set
//   rd_en       captures the muxed word into last_rd on the next edge
//   rd_data     muxed read word
//   last_rd     most recent captured word
//
// Both banks are 32 bits wide and share one address decode upstream.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [31:0] rd_a,
  input  logic [31:0] rd_b,
  input  logic        sel_b,
  input  logic        rd_en,
  output logic [31:0] rd_data,
  output logic [31:0] last_rd
);
  always @(rd_a or rd_b)
    rd_data = sel_b ? rd_b : rd_a;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)     last_rd <= 32'h0;
    else if (rd_en) last_rd <= rd_data;
  end
endmodule
