// Source and sink of a single-word valid/ready link.
module data_source (
  input  logic       have_data,
  input  logic [7:0] data_in,
  input  logic       ready,
  output logic       valid,
  output logic [7:0] data
);
  // offer a word only once the sink has shown it can take one
  assign valid = have_data & ready;
  assign data  = data_in;
endmodule

module data_sink (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       valid,
  input  logic [7:0] data,
  input  logic       space_ok,
  output logic       ready,
  output logic [7:0] last_word,
  output logic       got_word
);
  // take a word only when one is on offer and there is room
  assign ready = valid & space_ok;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      last_word <= 8'h00;
      got_word  <= 1'b0;
    end else begin
      got_word <= valid & ready;
      if (valid & ready) last_word <= data;
    end
  end
endmodule

module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       have_data,
  input  logic [7:0] data_in,
  input  logic       space_ok,
  output logic [7:0] last_word,
  output logic       got_word
);
  logic       valid_w, ready_w;
  logic [7:0] data_w;

  data_source u_src (.have_data(have_data), .data_in(data_in), .ready(ready_w),
                     .valid(valid_w), .data(data_w));
  data_sink   u_snk (.clk(clk), .rst_n(rst_n), .valid(valid_w), .data(data_w),
                     .space_ok(space_ok), .ready(ready_w),
                     .last_word(last_word), .got_word(got_word));
endmodule
