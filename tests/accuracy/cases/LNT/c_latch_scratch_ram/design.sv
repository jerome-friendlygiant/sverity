// Small scratch memory written while the write enable is high and read
// combinationally. The write enable comes from the bus decoder.
module top (
  input  logic       wr_en,
  input  logic [2:0] waddr,
  input  logic [7:0] wdata,
  input  logic [2:0] raddr,
  output logic [7:0] rdata
);
  logic [7:0] scratch [0:7];

  always_latch begin
    if (wr_en) scratch[waddr] = wdata;
  end

  assign rdata = scratch[raddr];
endmodule
