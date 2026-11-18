`default_nettype none
// Chip-select decoder with a write-enabled register behind it.
module cs_match #(
  parameter logic [15:0] BASE = 16'h8000
) (
  input  wire [15:0] addr,
  output wire        hit
);
  assign hit = (addr[15:12] == BASE[15:12]);
endmodule

module top (
  input  wire        clk,
  input  wire        rst_n,
  input  wire [15:0] addr,
  input  wire        wr,
  input  wire [7:0]  wdata,
  output wire [7:0]  cfg
);
  wire       hit_w;
  wire       wr_hit;
  reg  [7:0] cfg_q;

  cs_match #(.BASE(16'h8000)) u_cs (
    .addr (addr),
    .hit  (hit_w)
  );

  assign wr_hit = wr & hit_w;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      cfg_q <= 8'h00;
    else if (wr_hit) cfg_q <= wdata;
  end

  assign cfg = cfg_q;
endmodule
`default_nettype wire
