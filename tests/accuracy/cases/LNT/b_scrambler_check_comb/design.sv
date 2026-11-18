// Descrambles a data byte with a key and checks its parity bit.
// Interface:
//   raw, key  received byte and descrambling key
//   par_in    received even-parity bit over the descrambled byte
//   descr     descrambled byte
//   parity_ok parity of descr equals par_in
// Purely combinational.
module top (
  input  logic [7:0] raw,
  input  logic [7:0] key,
  input  logic       par_in,
  output logic [7:0] descr,
  output logic       parity_ok,
  output logic       par_calc,
  output logic       all_zero
);
  logic [7:0] scrambled;

  always_comb begin
    parity_ok = ((^scrambled) == par_in);
    scrambled = raw ^ key;
  end

  assign descr    = scrambled;
  assign par_calc = ^scrambled;
  assign all_zero = (scrambled == 8'h00);
endmodule
