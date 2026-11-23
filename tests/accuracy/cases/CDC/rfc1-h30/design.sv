// Single-clock register-file buffer.
module req_stage (
  input  logic       clk,
  input  logic       we_i,
  input  logic [3:0] waddr_i,
  input  logic [7:0] wdata_i,
  input  logic [3:0] raddr_i,
  output logic       we_q,
  output logic [3:0] waddr_q,
  output logic [7:0] wdata_q,
  output logic [3:0] raddr_q
);
  always_ff @(posedge clk) begin
    we_q    <= we_i;
    waddr_q <= waddr_i;
    wdata_q <= wdata_i;
    raddr_q <= raddr_i;
  end
endmodule

module sram_1r1w (
  input  logic       clk,
  input  logic       rst_ni,
  input  logic       we_i,
  input  logic [3:0] waddr_i,
  input  logic [7:0] wdata_i,
  input  logic [3:0] raddr_i,
  output logic [7:0] rdata_q
);
  logic [7:0] mem [0:15];

  always_ff @(posedge clk) begin
    if (we_i) mem[waddr_i] <= wdata_i;
  end

  always_ff @(posedge clk) begin
    if (!rst_ni) rdata_q <= 8'h00;
    else         rdata_q <= mem[raddr_i];
  end
endmodule

module top (
  input  logic       clk_a,
  input  logic       rst_ni,
  input  logic       we_i,
  input  logic [3:0] waddr_i,
  input  logic [7:0] wdata_i,
  input  logic [3:0] raddr_i,
  output logic [7:0] rdata_o
);
  logic       we_q;
  logic [3:0] waddr_q;
  logic [7:0] wdata_q;
  logic [3:0] raddr_q;

  req_stage u_req (.clk(clk_a), .we_i(we_i), .waddr_i(waddr_i), .wdata_i(wdata_i), .raddr_i(raddr_i),
                   .we_q(we_q), .waddr_q(waddr_q), .wdata_q(wdata_q), .raddr_q(raddr_q));

  sram_1r1w u_ram (.clk(clk_a), .rst_ni(rst_ni), .we_i(we_q), .waddr_i(waddr_q), .wdata_i(wdata_q),
                   .raddr_i(raddr_q), .rdata_q(rdata_o));
endmodule
